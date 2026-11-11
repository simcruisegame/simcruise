// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseGltfModel.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

int64 FSimCruiseGltfModel::NumTriangles() const
{
	int64 Count = 0;
	for (const FSimCruiseGltfPrimitive& Primitive : Primitives)
	{
		Count += Primitive.NumTriangles();
	}
	return Count;
}

namespace SimCruiseGltfPrivate
{
	struct FBufferView
	{
		int32 Buffer = 0;
		int64 ByteOffset = 0;
		int64 ByteLength = 0;
		int32 ByteStride = 0;
	};

	struct FAccessor
	{
		int32 BufferView = INDEX_NONE;
		int64 ByteOffset = 0;
		int32 ComponentType = 0;
		bool bNormalized = false;
		int64 Count = 0;
		int32 NumComponents = 1;
		bool bSparse = false;
	};

	struct FContext
	{
		FString Path;
		FString Directory;
		TArray<TArray<uint8>> Buffers;
		TArray<FBufferView> Views;
		TArray<FAccessor> Accessors;
		TArray<int32> TextureToImage;
		TSharedPtr<FJsonObject> Root;
		FString Error;

		bool Fail(const FString& Where, const FString& Why)
		{
			Error = FString::Printf(TEXT("%s: %s: %s"), *Path, *Where, *Why);
			return false;
		}
	};

	/** Percent-decoding for relative URIs ("textures/Seats%20Leather.png"). */
	static FString UriDecode(const FString& Uri)
	{
		TArray<ANSICHAR> Bytes;
		const FTCHARToUTF8 Utf8(*Uri);
		for (int32 I = 0; I < Utf8.Length(); ++I)
		{
			const ANSICHAR C = Utf8.Get()[I];
			if (C == '%' && I + 2 < Utf8.Length() && FChar::IsHexDigit(Utf8.Get()[I + 1]) && FChar::IsHexDigit(Utf8.Get()[I + 2]))
			{
				Bytes.Add(static_cast<ANSICHAR>(FParse::HexDigit(Utf8.Get()[I + 1]) * 16 + FParse::HexDigit(Utf8.Get()[I + 2])));
				I += 2;
			}
			else
			{
				Bytes.Add(C);
			}
		}
		const FUTF8ToTCHAR Decoded(Bytes.GetData(), Bytes.Num());
		return FString(Decoded.Length(), Decoded.Get());
	}

	static int32 ComponentCount(const FString& Type)
	{
		if (Type == TEXT("SCALAR")) return 1;
		if (Type == TEXT("VEC2")) return 2;
		if (Type == TEXT("VEC3")) return 3;
		if (Type == TEXT("VEC4")) return 4;
		if (Type == TEXT("MAT4")) return 16;
		return 0;
	}

	static int32 ComponentSize(int32 ComponentType)
	{
		switch (ComponentType)
		{
		case 5120: case 5121: return 1;
		case 5122: case 5123: return 2;
		case 5125: case 5126: return 4;
		default: return 0;
		}
	}

	static double GetNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Default)
	{
		double Value = Default;
		if (Object.IsValid())
		{
			Object->TryGetNumberField(Key, Value);
		}
		return Value;
	}

	static int32 GetInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32 Default)
	{
		return static_cast<int32>(GetNumber(Object, Key, static_cast<double>(Default)));
	}

	static const TArray<TSharedPtr<FJsonValue>>& GetArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		static const TArray<TSharedPtr<FJsonValue>> Empty;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Key, Array) && Array)
		{
			return *Array;
		}
		return Empty;
	}

	static TSharedPtr<FJsonObject> GetObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Child = nullptr;
		if (Object.IsValid() && Object->TryGetObjectField(Key, Child) && Child)
		{
			return *Child;
		}
		return nullptr;
	}

	/** Read accessor element I, component C, as a double (normalized when flagged). */
	static double ReadComponent(const uint8* Data, int32 ComponentType, bool bNormalized)
	{
		switch (ComponentType)
		{
		case 5126: { float V; FMemory::Memcpy(&V, Data, 4); return V; }
		case 5125: { uint32 V; FMemory::Memcpy(&V, Data, 4); return V; }
		case 5123: { uint16 V; FMemory::Memcpy(&V, Data, 2); return bNormalized ? V / 65535.0 : V; }
		case 5122: { int16 V; FMemory::Memcpy(&V, Data, 2); return bNormalized ? FMath::Max(V / 32767.0, -1.0) : V; }
		case 5121: { uint8 V = *Data; return bNormalized ? V / 255.0 : V; }
		case 5120: { int8 V; FMemory::Memcpy(&V, Data, 1); return bNormalized ? FMath::Max(V / 127.0, -1.0) : V; }
		default: return 0.0;
		}
	}

	/** Visit every element of an accessor: Visit(ElementIndex, const double* Components). */
	template <typename FVisit>
	static bool ReadAccessor(FContext& Context, int32 AccessorIndex, int32 ExpectedComponents, FVisit&& Visit)
	{
		if (!Context.Accessors.IsValidIndex(AccessorIndex))
		{
			return Context.Fail(TEXT("accessor"), FString::Printf(TEXT("index %d out of range"), AccessorIndex));
		}
		const FAccessor& Accessor = Context.Accessors[AccessorIndex];
		if (Accessor.bSparse)
		{
			return Context.Fail(FString::Printf(TEXT("accessor %d"), AccessorIndex), TEXT("sparse accessors are not supported"));
		}
		if (ExpectedComponents > 0 && Accessor.NumComponents != ExpectedComponents)
		{
			return Context.Fail(FString::Printf(TEXT("accessor %d"), AccessorIndex),
				FString::Printf(TEXT("has %d components, expected %d"), Accessor.NumComponents, ExpectedComponents));
		}
		double Components[16] = {};
		if (Accessor.BufferView == INDEX_NONE)
		{
			// All zeros per the specification.
			for (int64 I = 0; I < Accessor.Count; ++I)
			{
				Visit(I, Components);
			}
			return true;
		}
		if (!Context.Views.IsValidIndex(Accessor.BufferView))
		{
			return Context.Fail(FString::Printf(TEXT("accessor %d"), AccessorIndex), TEXT("bufferView out of range"));
		}
		const FBufferView& View = Context.Views[Accessor.BufferView];
		if (!Context.Buffers.IsValidIndex(View.Buffer))
		{
			return Context.Fail(FString::Printf(TEXT("bufferView %d"), Accessor.BufferView), TEXT("buffer out of range"));
		}
		const TArray<uint8>& Buffer = Context.Buffers[View.Buffer];
		const int32 ElementSize = ComponentSize(Accessor.ComponentType) * Accessor.NumComponents;
		if (ElementSize == 0)
		{
			return Context.Fail(FString::Printf(TEXT("accessor %d"), AccessorIndex), TEXT("unknown component type"));
		}
		const int64 Stride = View.ByteStride > 0 ? View.ByteStride : ElementSize;
		const int64 Start = View.ByteOffset + Accessor.ByteOffset;
		const int64 End = Accessor.Count > 0 ? Start + Stride * (Accessor.Count - 1) + ElementSize : Start;
		if (Start < 0 || End > Buffer.Num() || End > View.ByteOffset + View.ByteLength)
		{
			return Context.Fail(FString::Printf(TEXT("accessor %d"), AccessorIndex), TEXT("reads past the end of its buffer view"));
		}
		const int32 Size = ComponentSize(Accessor.ComponentType);
		for (int64 I = 0; I < Accessor.Count; ++I)
		{
			const uint8* Element = Buffer.GetData() + Start + Stride * I;
			for (int32 C = 0; C < Accessor.NumComponents; ++C)
			{
				Components[C] = ReadComponent(Element + C * Size, Accessor.ComponentType, Accessor.bNormalized);
			}
			Visit(I, Components);
		}
		return true;
	}

	static bool LoadBuffers(FContext& Context, const TArray<uint8>* GlbBinary)
	{
		const TArray<TSharedPtr<FJsonValue>>& Buffers = GetArray(Context.Root, TEXT("buffers"));
		for (int32 Index = 0; Index < Buffers.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Buffer = Buffers[Index]->AsObject();
			FString Uri;
			TArray<uint8>& Data = Context.Buffers.AddDefaulted_GetRef();
			if (!Buffer.IsValid() || !Buffer->TryGetStringField(TEXT("uri"), Uri))
			{
				if (Index == 0 && GlbBinary)
				{
					Data = *GlbBinary;
					continue;
				}
				return Context.Fail(FString::Printf(TEXT("buffer %d"), Index), TEXT("has no uri"));
			}
			if (Uri.StartsWith(TEXT("data:")))
			{
				int32 Comma = INDEX_NONE;
				if (!Uri.FindChar(TEXT(','), Comma) || !FBase64::Decode(Uri.Mid(Comma + 1), Data))
				{
					return Context.Fail(FString::Printf(TEXT("buffer %d"), Index), TEXT("bad data URI"));
				}
				continue;
			}
			const FString File = FPaths::Combine(Context.Directory, UriDecode(Uri));
			if (!FFileHelper::LoadFileToArray(Data, *File))
			{
				return Context.Fail(FString::Printf(TEXT("buffer %d"), Index), FString::Printf(TEXT("cannot read %s"), *File));
			}
		}
		return true;
	}

	static FMatrix NodeLocalMatrix(const TSharedPtr<FJsonObject>& Node)
	{
		const TArray<TSharedPtr<FJsonValue>>& MatrixValues = GetArray(Node, TEXT("matrix"));
		if (MatrixValues.Num() == 16)
		{
			// glTF stores column-major for column vectors; that is exactly the
			// row-major layout of the row-vector FMatrix: M[i][j] = m[i*4+j].
			FMatrix M;
			for (int32 I = 0; I < 4; ++I)
			{
				for (int32 J = 0; J < 4; ++J)
				{
					M.M[I][J] = MatrixValues[I * 4 + J]->AsNumber();
				}
			}
			return M;
		}
		FVector T(0.0);
		FQuat R = FQuat::Identity;
		FVector S(1.0);
		const TArray<TSharedPtr<FJsonValue>>& TV = GetArray(Node, TEXT("translation"));
		const TArray<TSharedPtr<FJsonValue>>& RV = GetArray(Node, TEXT("rotation"));
		const TArray<TSharedPtr<FJsonValue>>& SV = GetArray(Node, TEXT("scale"));
		if (TV.Num() == 3) T = FVector(TV[0]->AsNumber(), TV[1]->AsNumber(), TV[2]->AsNumber());
		if (RV.Num() == 4) R = FQuat(RV[0]->AsNumber(), RV[1]->AsNumber(), RV[2]->AsNumber(), RV[3]->AsNumber());
		if (SV.Num() == 3) S = FVector(SV[0]->AsNumber(), SV[1]->AsNumber(), SV[2]->AsNumber());
		R.Normalize();
		// Column-vector M = T * R * S. As a row-vector matrix: v * S * R^T... build explicitly.
		// The rotation matrix of a glTF (right-handed) quaternion, column-vector form.
		const double X = R.X, Y = R.Y, Z = R.Z, W = R.W;
		const double Rc[3][3] = {
			{1 - 2 * (Y * Y + Z * Z), 2 * (X * Y - Z * W), 2 * (X * Z + Y * W)},
			{2 * (X * Y + Z * W), 1 - 2 * (X * X + Z * Z), 2 * (Y * Z - X * W)},
			{2 * (X * Z - Y * W), 2 * (Y * Z + X * W), 1 - 2 * (X * X + Y * Y)}};
		FMatrix M = FMatrix::Identity;
		// Row-vector M[i][j] = column-vector Mc[j][i]; Mc = Rc * diag(S).
		for (int32 I = 0; I < 3; ++I)
		{
			for (int32 J = 0; J < 3; ++J)
			{
				M.M[I][J] = Rc[J][I] * S[I];
			}
		}
		M.M[3][0] = T.X;
		M.M[3][1] = T.Y;
		M.M[3][2] = T.Z;
		return M;
	}

	static int32 TextureImage(FContext& Context, const TSharedPtr<FJsonObject>& Info)
	{
		if (!Info.IsValid())
		{
			return INDEX_NONE;
		}
		const int32 Texture = GetInt(Info, TEXT("index"), INDEX_NONE);
		return Context.TextureToImage.IsValidIndex(Texture) ? Context.TextureToImage[Texture] : INDEX_NONE;
	}

	static FLinearColor ReadColor(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, const FLinearColor& Default)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = GetArray(Object, Key);
		if (Values.Num() >= 3)
		{
			return FLinearColor(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber(),
				Values.Num() >= 4 ? Values[3]->AsNumber() : 1.0);
		}
		return Default;
	}

	static bool ReadPrimitive(FContext& Context, const TSharedPtr<FJsonObject>& Primitive, FSimCruiseGltfPrimitive& Out, const FString& Where)
	{
		const int32 Mode = GetInt(Primitive, TEXT("mode"), 4);
		if (Mode != 4 && Mode != 5 && Mode != 6)
		{
			return true; // points and lines are not car surfaces
		}
		const TSharedPtr<FJsonObject> Attributes = GetObject(Primitive, TEXT("attributes"));
		const int32 PositionAccessor = GetInt(Attributes, TEXT("POSITION"), INDEX_NONE);
		if (PositionAccessor == INDEX_NONE)
		{
			return Context.Fail(Where, TEXT("primitive without POSITION"));
		}
		Out.MaterialIndex = GetInt(Primitive, TEXT("material"), INDEX_NONE);
		bool bOk = ReadAccessor(Context, PositionAccessor, 3, [&Out](int64, const double* C)
		{
			Out.Positions.Emplace(C[0], C[1], C[2]);
		});
		const int32 NormalAccessor = GetInt(Attributes, TEXT("NORMAL"), INDEX_NONE);
		if (bOk && NormalAccessor != INDEX_NONE)
		{
			bOk = ReadAccessor(Context, NormalAccessor, 3, [&Out](int64, const double* C)
			{
				Out.Normals.Emplace(C[0], C[1], C[2]);
			});
		}
		const int32 TangentAccessor = GetInt(Attributes, TEXT("TANGENT"), INDEX_NONE);
		if (bOk && TangentAccessor != INDEX_NONE)
		{
			bOk = ReadAccessor(Context, TangentAccessor, 4, [&Out](int64, const double* C)
			{
				Out.Tangents.Emplace(C[0], C[1], C[2], C[3]);
			});
		}
		const int32 UVAccessor = GetInt(Attributes, TEXT("TEXCOORD_0"), INDEX_NONE);
		if (bOk && UVAccessor != INDEX_NONE)
		{
			bOk = ReadAccessor(Context, UVAccessor, 2, [&Out](int64, const double* C)
			{
				Out.UV0.Emplace(C[0], C[1]);
			});
		}
		if (!bOk)
		{
			return false;
		}
		const int32 VertexCount = Out.Positions.Num();
		if (Out.Normals.Num() != 0 && Out.Normals.Num() != VertexCount) Out.Normals.Reset();
		if (Out.Tangents.Num() != 0 && Out.Tangents.Num() != VertexCount) Out.Tangents.Reset();
		if (Out.UV0.Num() != 0 && Out.UV0.Num() != VertexCount) Out.UV0.Reset();

		TArray<uint32> Raw;
		const int32 IndexAccessor = GetInt(Primitive, TEXT("indices"), INDEX_NONE);
		if (IndexAccessor != INDEX_NONE)
		{
			if (!ReadAccessor(Context, IndexAccessor, 1, [&Raw](int64, const double* C)
			{
				Raw.Add(static_cast<uint32>(C[0]));
			}))
			{
				return false;
			}
		}
		else
		{
			Raw.SetNumUninitialized(VertexCount);
			for (int32 I = 0; I < VertexCount; ++I) Raw[I] = I;
		}
		for (uint32 Index : Raw)
		{
			if (Index >= static_cast<uint32>(VertexCount))
			{
				return Context.Fail(Where, FString::Printf(TEXT("index %u out of range (%d vertices)"), Index, VertexCount));
			}
		}
		if (Mode == 4)
		{
			Raw.SetNum(Raw.Num() - Raw.Num() % 3);
			Out.Indices = MoveTemp(Raw);
		}
		else if (Mode == 5)
		{
			for (int32 I = 2; I < Raw.Num(); ++I)
			{
				const bool bOdd = (I % 2) == 1;
				Out.Indices.Append({Raw[I - 2], bOdd ? Raw[I] : Raw[I - 1], bOdd ? Raw[I - 1] : Raw[I]});
			}
		}
		else
		{
			for (int32 I = 2; I < Raw.Num(); ++I)
			{
				Out.Indices.Append({Raw[0], Raw[I - 1], Raw[I]});
			}
		}
		return true;
	}
}

bool SimCruiseGltf::Load(const FString& Path, FSimCruiseGltfModel& OutModel, FString& OutError)
{
	using namespace SimCruiseGltfPrivate;
	OutModel = FSimCruiseGltfModel();
	OutModel.SourcePath = Path;

	FContext Context;
	Context.Path = Path;
	Context.Directory = FPaths::GetPath(Path);

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Path))
	{
		OutError = FString::Printf(TEXT("%s: cannot read the file"), *Path);
		return false;
	}

	FString JsonText;
	TArray<uint8> GlbBinary;
	bool bGlb = false;
	if (FileData.Num() >= 12 && FMemory::Memcmp(FileData.GetData(), "glTF", 4) == 0)
	{
		bGlb = true;
		int64 Offset = 12;
		while (Offset + 8 <= FileData.Num())
		{
			uint32 ChunkLength = 0, ChunkType = 0;
			FMemory::Memcpy(&ChunkLength, FileData.GetData() + Offset, 4);
			FMemory::Memcpy(&ChunkType, FileData.GetData() + Offset + 4, 4);
			Offset += 8;
			if (Offset + ChunkLength > FileData.Num())
			{
				OutError = FString::Printf(TEXT("%s: truncated GLB chunk"), *Path);
				return false;
			}
			if (ChunkType == 0x4E4F534A) // JSON
			{
				FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(FileData.GetData() + Offset), ChunkLength);
				JsonText = FString(Converter.Length(), Converter.Get());
			}
			else if (ChunkType == 0x004E4942) // BIN
			{
				GlbBinary.Append(FileData.GetData() + Offset, ChunkLength);
			}
			Offset += ChunkLength;
		}
	}
	else
	{
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(FileData.GetData()), FileData.Num());
		JsonText = FString(Converter.Length(), Converter.Get());
	}
	FileData.Empty();

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Context.Root) || !Context.Root.IsValid())
	{
		OutError = FString::Printf(TEXT("%s: invalid JSON: %s"), *Path, *Reader->GetErrorMessage());
		return false;
	}
	const TSharedPtr<FJsonObject> Asset = GetObject(Context.Root, TEXT("asset"));
	FString Version;
	if (!Asset.IsValid() || !Asset->TryGetStringField(TEXT("version"), Version) || !Version.StartsWith(TEXT("2")))
	{
		OutError = FString::Printf(TEXT("%s: asset.version must be 2.x"), *Path);
		return false;
	}
	if (const TSharedPtr<FJsonObject> Extras = GetObject(Asset, TEXT("extras")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Extras->Values)
		{
			FString Value;
			if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value))
			{
				OutModel.AssetExtras.Add(Pair.Key, Value);
			}
		}
	}

	if (!LoadBuffers(Context, bGlb ? &GlbBinary : nullptr))
	{
		OutError = Context.Error;
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : GetArray(Context.Root, TEXT("bufferViews")))
	{
		const TSharedPtr<FJsonObject> View = Value->AsObject();
		FBufferView& Out = Context.Views.AddDefaulted_GetRef();
		Out.Buffer = GetInt(View, TEXT("buffer"), 0);
		Out.ByteOffset = static_cast<int64>(GetNumber(View, TEXT("byteOffset"), 0));
		Out.ByteLength = static_cast<int64>(GetNumber(View, TEXT("byteLength"), 0));
		Out.ByteStride = GetInt(View, TEXT("byteStride"), 0);
	}
	for (const TSharedPtr<FJsonValue>& Value : GetArray(Context.Root, TEXT("accessors")))
	{
		const TSharedPtr<FJsonObject> Accessor = Value->AsObject();
		FAccessor& Out = Context.Accessors.AddDefaulted_GetRef();
		Out.BufferView = GetInt(Accessor, TEXT("bufferView"), INDEX_NONE);
		Out.ByteOffset = static_cast<int64>(GetNumber(Accessor, TEXT("byteOffset"), 0));
		Out.ComponentType = GetInt(Accessor, TEXT("componentType"), 0);
		Out.Count = static_cast<int64>(GetNumber(Accessor, TEXT("count"), 0));
		Accessor->TryGetBoolField(TEXT("normalized"), Out.bNormalized);
		FString Type;
		Accessor->TryGetStringField(TEXT("type"), Type);
		Out.NumComponents = ComponentCount(Type);
		Out.bSparse = Accessor->HasField(TEXT("sparse"));
	}

	// Images and textures: only external files are used by the car models.
	for (const TSharedPtr<FJsonValue>& Value : GetArray(Context.Root, TEXT("images")))
	{
		FString Uri;
		Value->AsObject()->TryGetStringField(TEXT("uri"), Uri);
		OutModel.ImageFiles.Add(Uri.IsEmpty() || Uri.StartsWith(TEXT("data:"))
			? FString()
			: FPaths::ConvertRelativePathToFull(FPaths::Combine(Context.Directory, UriDecode(Uri))));
	}
	for (const TSharedPtr<FJsonValue>& Value : GetArray(Context.Root, TEXT("textures")))
	{
		Context.TextureToImage.Add(GetInt(Value->AsObject(), TEXT("source"), INDEX_NONE));
	}

	for (const TSharedPtr<FJsonValue>& Value : GetArray(Context.Root, TEXT("materials")))
	{
		const TSharedPtr<FJsonObject> Material = Value->AsObject();
		FSimCruiseGltfMaterial& Out = OutModel.Materials.AddDefaulted_GetRef();
		Material->TryGetStringField(TEXT("name"), Out.Name);
		Material->TryGetStringField(TEXT("alphaMode"), Out.AlphaMode);
		Out.AlphaCutoff = GetNumber(Material, TEXT("alphaCutoff"), 0.5);
		Material->TryGetBoolField(TEXT("doubleSided"), Out.bDoubleSided);
		Out.EmissiveFactor = ReadColor(Material, TEXT("emissiveFactor"), FLinearColor::Black);
		Out.EmissiveImage = TextureImage(Context, GetObject(Material, TEXT("emissiveTexture")));
		const TSharedPtr<FJsonObject> Normal = GetObject(Material, TEXT("normalTexture"));
		Out.NormalImage = TextureImage(Context, Normal);
		Out.NormalScale = GetNumber(Normal, TEXT("scale"), 1.0);
		Out.OcclusionImage = TextureImage(Context, GetObject(Material, TEXT("occlusionTexture")));
		if (const TSharedPtr<FJsonObject> Pbr = GetObject(Material, TEXT("pbrMetallicRoughness")))
		{
			Out.BaseColorFactor = ReadColor(Pbr, TEXT("baseColorFactor"), FLinearColor::White);
			Out.MetallicFactor = GetNumber(Pbr, TEXT("metallicFactor"), 1.0);
			Out.RoughnessFactor = GetNumber(Pbr, TEXT("roughnessFactor"), 1.0);
			Out.BaseColorImage = TextureImage(Context, GetObject(Pbr, TEXT("baseColorTexture")));
			Out.MetallicRoughnessImage = TextureImage(Context, GetObject(Pbr, TEXT("metallicRoughnessTexture")));
		}
		if (const TSharedPtr<FJsonObject> Extensions = GetObject(Material, TEXT("extensions")))
		{
			if (const TSharedPtr<FJsonObject> Clearcoat = GetObject(Extensions, TEXT("KHR_materials_clearcoat")))
			{
				Out.ClearcoatFactor = GetNumber(Clearcoat, TEXT("clearcoatFactor"), 0.0);
				Out.ClearcoatRoughnessFactor = GetNumber(Clearcoat, TEXT("clearcoatRoughnessFactor"), 0.0);
			}
			if (const TSharedPtr<FJsonObject> Transmission = GetObject(Extensions, TEXT("KHR_materials_transmission")))
			{
				Out.TransmissionFactor = GetNumber(Transmission, TEXT("transmissionFactor"), 0.0);
				Out.TransmissionImage = TextureImage(Context, GetObject(Transmission, TEXT("transmissionTexture")));
			}
		}
	}

	// Walk the default scene's node tree, accumulating transforms and names.
	const TArray<TSharedPtr<FJsonValue>>& Nodes = GetArray(Context.Root, TEXT("nodes"));
	const TArray<TSharedPtr<FJsonValue>>& Meshes = GetArray(Context.Root, TEXT("meshes"));
	const TArray<TSharedPtr<FJsonValue>>& Scenes = GetArray(Context.Root, TEXT("scenes"));
	const int32 SceneIndex = GetInt(Context.Root, TEXT("scene"), 0);
	if (!Scenes.IsValidIndex(SceneIndex))
	{
		OutError = FString::Printf(TEXT("%s: no scene %d"), *Path, SceneIndex);
		return false;
	}
	struct FPending
	{
		int32 Node;
		FMatrix ParentToRoot;
		FString ParentLabel;
		int32 Depth;
	};
	TArray<FPending> Stack;
	for (const TSharedPtr<FJsonValue>& Root : GetArray(Scenes[SceneIndex]->AsObject(), TEXT("nodes")))
	{
		Stack.Add({static_cast<int32>(Root->AsNumber()), FMatrix::Identity, FString(), 0});
	}
	while (Stack.Num() > 0)
	{
		const FPending Item = Stack.Pop(EAllowShrinking::No);
		if (!Nodes.IsValidIndex(Item.Node) || Item.Depth > 64)
		{
			OutError = FString::Printf(TEXT("%s: node %d out of range or nested too deep"), *Path, Item.Node);
			return false;
		}
		const TSharedPtr<FJsonObject> Node = Nodes[Item.Node]->AsObject();
		FString Name;
		Node->TryGetStringField(TEXT("name"), Name);
		const FMatrix NodeToRoot = NodeLocalMatrix(Node) * Item.ParentToRoot;
		// Like ModelRig.gd: the node's own name first, then its ancestors'.
		const FString Label = TEXT(" ") + Name.ToLower() + Item.ParentLabel;
		const int32 MeshIndex = GetInt(Node, TEXT("mesh"), INDEX_NONE);
		if (MeshIndex != INDEX_NONE)
		{
			if (!Meshes.IsValidIndex(MeshIndex))
			{
				OutError = FString::Printf(TEXT("%s: node '%s' mesh %d out of range"), *Path, *Name, MeshIndex);
				return false;
			}
			const TSharedPtr<FJsonObject> Mesh = Meshes[MeshIndex]->AsObject();
			FString MeshName;
			Mesh->TryGetStringField(TEXT("name"), MeshName);
			const TArray<TSharedPtr<FJsonValue>>& Primitives = GetArray(Mesh, TEXT("primitives"));
			for (int32 P = 0; P < Primitives.Num(); ++P)
			{
				FSimCruiseGltfPrimitive Primitive;
				Primitive.NodeName = Name;
				Primitive.OwnerLabel = Label + TEXT(" ") + MeshName.ToLower();
				Primitive.NodeToRoot = NodeToRoot;
				if (!ReadPrimitive(Context, Primitives[P]->AsObject(), Primitive,
					FString::Printf(TEXT("mesh %d '%s' primitive %d"), MeshIndex, *MeshName, P)))
				{
					OutError = Context.Error;
					return false;
				}
				if (Primitive.Indices.Num() > 0)
				{
					OutModel.Primitives.Add(MoveTemp(Primitive));
				}
			}
		}
		for (const TSharedPtr<FJsonValue>& Child : GetArray(Node, TEXT("children")))
		{
			Stack.Add({static_cast<int32>(Child->AsNumber()), NodeToRoot, Label, Item.Depth + 1});
		}
	}
	if (OutModel.Primitives.Num() == 0)
	{
		OutError = FString::Printf(TEXT("%s: the scene has no triangle meshes"), *Path);
		return false;
	}
	return true;
}
