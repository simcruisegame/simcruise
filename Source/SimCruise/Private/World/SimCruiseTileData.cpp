// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseTileData.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ReadPoint(const TSharedPtr<FJsonValue>& Value, FVector2d& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Pair) || Pair->Num() < 2)
		{
			return false;
		}
		double X = 0.0, Y = 0.0;
		if (!(*Pair)[0]->TryGetNumber(X) || !(*Pair)[1]->TryGetNumber(Y) || !FMath::IsFinite(X) || !FMath::IsFinite(Y))
		{
			return false;
		}
		Out = FVector2d(X, Y);
		return true;
	}

	bool ReadLine(const TSharedPtr<FJsonValue>& Value, TArray<FVector2d>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Points))
		{
			return false;
		}
		Out.Reset(Points->Num());
		for (const TSharedPtr<FJsonValue>& P : *Points)
		{
			FVector2d Point;
			if (!ReadPoint(P, Point))
			{
				return false;
			}
			Out.Add(Point);
		}
		return true;
	}

	FString StringAt(const TArray<TSharedPtr<FJsonValue>>& Row, int32 Index)
	{
		FString S;
		if (Row.IsValidIndex(Index) && Row[Index].IsValid())
		{
			Row[Index]->TryGetString(S);
		}
		return S;
	}

	double NumberAt(const TArray<TSharedPtr<FJsonValue>>& Row, int32 Index, double Default)
	{
		double N = Default;
		if (Row.IsValidIndex(Index) && Row[Index].IsValid() && Row[Index]->TryGetNumber(N) && FMath::IsFinite(N))
		{
			return N;
		}
		return Default;
	}

	/** The first vertex of a footprint nested [[[x,y],...]] (or deeper). */
	bool FirstVertex(const TSharedPtr<FJsonValue>& Value, FVector2d& Out, int32 Depth = 0)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Arr) || Arr->Num() == 0 || Depth > 4)
		{
			return false;
		}
		double X = 0.0;
		if ((*Arr)[0]->TryGetNumber(X))
		{
			return ReadPoint(Value, Out);
		}
		return FirstVertex((*Arr)[0], Out, Depth + 1);
	}

	/** The outer ring of a footprint [[[x,y],...]] (polygon of rings). */
	bool OuterRing(const TSharedPtr<FJsonValue>& Value, TArray<FVector2d>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rings = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Rings) || Rings->Num() == 0)
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* First = nullptr;
		if (!(*Rings)[0]->TryGetArray(First) || First->Num() == 0)
		{
			return false;
		}
		double Probe = 0.0;
		if ((*First)[0]->TryGetNumber(Probe))
		{
			return ReadLine(Value, Out); // already a ring of points
		}
		return OuterRing((*Rings)[0], Out) || ReadLine((*Rings)[0], Out);
	}
}

double FSimCruiseTileData::HeightAtUnit(const FVector2d& Unit) const
{
	if (!IsValid())
	{
		return 0.0;
	}
	const double Gx = FMath::Clamp(Unit.X * Grid, 0.0, static_cast<double>(Grid));
	const double Gy = FMath::Clamp(Unit.Y * Grid, 0.0, static_cast<double>(Grid));
	const int32 X = FMath::Min(static_cast<int32>(Gx), Grid - 1);
	const int32 Y = FMath::Min(static_cast<int32>(Gy), Grid - 1);
	const int32 Stride = Grid + 1;
	const int32 I = Y * Stride + X;
	const double U = Gx - X;
	const double V = Gy - Y;
	const double H00 = Heights[I], H10 = Heights[I + 1], H01 = Heights[I + Stride], H11 = Heights[I + Stride + 1];
	if (bTriangleHeights)
	{
		// The NE-SW diagonal (WorldTile.base_height, helper road-grade.mjs fine grid).
		if (U + V <= 1.0)
		{
			return H00 + (H10 - H00) * U + (H01 - H00) * V;
		}
		return H11 + (H10 - H11) * (1.0 - V) + (H01 - H11) * (1.0 - U);
	}
	return FMath::Lerp(FMath::Lerp(H00, H10, U), FMath::Lerp(H01, H11, U), V);
}

namespace SimCruiseTileData
{
	bool Parse(const FString& JsonText, FSimCruiseTileData& Out, FString& OutError)
	{
		Out = FSimCruiseTileData();
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("tile: not a JSON object (%s)"), *Reader->GetErrorMessage());
			return false;
		}
		return ParseObject(Root, Out, OutError);
	}

	bool ParseObject(const TSharedPtr<FJsonObject>& Root, FSimCruiseTileData& Out, FString& OutError)
	{
		Out = FSimCruiseTileData();
		if (!Root.IsValid())
		{
			OutError = TEXT("tile: not a JSON object");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Id = nullptr;
		if (Root->TryGetArrayField(TEXT("id"), Id) && Id->Num() == 2)
		{
			Out.Id = FIntPoint(static_cast<int32>((*Id)[0]->AsNumber()), static_cast<int32>((*Id)[1]->AsNumber()));
		}
		else
		{
			OutError = TEXT("tile.id: missing [x, y]");
			return false;
		}

		double GridNumber = 0.0;
		if (!Root->TryGetNumberField(TEXT("grid"), GridNumber))
		{
			GridNumber = 32.0; // the oldest documents had no grid field (WorldTile.grid_steps)
		}
		Out.Grid = static_cast<int32>(GridNumber);
		const TArray<TSharedPtr<FJsonValue>>* H = nullptr;
		if (Out.Grid <= 0 || Out.Grid > 1024 || !Root->TryGetArrayField(TEXT("h"), H) || H->Num() != (Out.Grid + 1) * (Out.Grid + 1))
		{
			OutError = FString::Printf(TEXT("tile.h: expected %d heights for grid %d"), (Out.Grid + 1) * (Out.Grid + 1), Out.Grid);
			return false;
		}
		Out.Heights.SetNumUninitialized(H->Num());
		for (int32 i = 0; i < H->Num(); ++i)
		{
			double V = 0.0;
			if (!(*H)[i]->TryGetNumber(V) || !FMath::IsFinite(V) || FMath::Abs(V) > 9000.0)
			{
				OutError = FString::Printf(TEXT("tile.h[%d]: not a height"), i);
				return false;
			}
			Out.Heights[i] = static_cast<float>(V);
		}

		const TSharedPtr<FJsonObject>* Grade = nullptr;
		if (Root->TryGetObjectField(TEXT("road_grade"), Grade))
		{
			double Version = 0.0;
			(*Grade)->TryGetNumberField(TEXT("version"), Version);
			Out.RoadGradeVersion = static_cast<int32>(Version);
			(*Grade)->TryGetStringField(TEXT("source"), Out.HeightSource);
			Out.bTriangleHeights = (*Grade)->HasField(TEXT("source"));
		}

		const TArray<TSharedPtr<FJsonValue>>* Roads = nullptr;
		if (!Root->TryGetArrayField(TEXT("roads"), Roads))
		{
			OutError = TEXT("tile.roads: missing");
			return false;
		}
		for (int32 r = 0; r < Roads->Num(); ++r)
		{
			const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
			if (!(*Roads)[r]->TryGetArray(Row) || Row->Num() < 5)
			{
				OutError = FString::Printf(TEXT("tile.roads[%d]: expected [class, oneway, name, points, width, ...]"), r);
				return false;
			}
			FSimCruiseRoadRow Road;
			Road.Class = StringAt(*Row, 0);
			Road.OneWay = static_cast<int32>(NumberAt(*Row, 1, 0.0));
			Road.Name = StringAt(*Row, 2);
			if (!ReadLine((*Row)[3], Road.Points))
			{
				OutError = FString::Printf(TEXT("tile.roads[%d][3]: bad points"), r);
				return false;
			}
			Road.WidthM = NumberAt(*Row, 4, 8.0);
			Road.Surface = StringAt(*Row, 5);
			Road.bBridge = StringAt(*Row, 6) == TEXT("bridge");
			if (Road.Points.Num() >= 2 && Road.WidthM > 0.5)
			{
				Out.Roads.Add(MoveTemp(Road));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Surface = nullptr;
		if (Root->TryGetArrayField(TEXT("road_surface"), Surface))
		{
			for (const TSharedPtr<FJsonValue>& ChainValue : *Surface)
			{
				const TSharedPtr<FJsonObject>* ChainObject = nullptr;
				if (!ChainValue->TryGetObject(ChainObject))
				{
					continue;
				}
				FSimCruiseSurfaceChain Chain;
				(*ChainObject)->TryGetStringField(TEXT("cls"), Chain.Class);
				(*ChainObject)->TryGetNumberField(TEXT("w"), Chain.WidthM);
				const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
				if ((*ChainObject)->TryGetArrayField(TEXT("pts"), Pts))
				{
					for (const TSharedPtr<FJsonValue>& PtValue : *Pts)
					{
						const TArray<TSharedPtr<FJsonValue>>* Pt = nullptr;
						if (!PtValue->TryGetArray(Pt) || Pt->Num() < 6)
						{
							continue;
						}
						FSimCruiseSurfaceStation S;
						S.P = FVector2d(NumberAt(*Pt, 0, 0.0), NumberAt(*Pt, 1, 0.0));
						S.Z = NumberAt(*Pt, 2, 0.0);
						S.E = NumberAt(*Pt, 3, 0.0);
						S.T = FVector2d(NumberAt(*Pt, 4, 1.0), NumberAt(*Pt, 5, 0.0));
						S.WidthM = NumberAt(*Pt, 6, Chain.WidthM);
						Chain.Stations.Add(S);
					}
				}
				if (Chain.Stations.Num() >= 2)
				{
					Out.RoadSurface.Add(MoveTemp(Chain));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Pedestrian = nullptr;
		if (Root->TryGetArrayField(TEXT("pedestrian"), Pedestrian))
		{
			for (const TSharedPtr<FJsonValue>& RowValue : *Pedestrian)
			{
				const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
				if (!RowValue->TryGetArray(Row) || Row->Num() < 3)
				{
					continue;
				}
				FSimCruisePathRow Path;
				Path.Kind = StringAt(*Row, 0);
				Path.WidthM = NumberAt(*Row, 1, 2.0);
				if (ReadLine((*Row)[2], Path.Points) && Path.Points.Num() >= 2)
				{
					Out.Pedestrian.Add(MoveTemp(Path));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Land = nullptr;
		if (Root->TryGetArrayField(TEXT("land"), Land))
		{
			for (const TSharedPtr<FJsonValue>& RowValue : *Land)
			{
				const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Rings = nullptr;
				if (!RowValue->TryGetArray(Row) || Row->Num() < 3 || !(*Row)[2]->TryGetArray(Rings))
				{
					continue;
				}
				FSimCruiseLandPolygon Polygon;
				Polygon.Layer = StringAt(*Row, 0);
				Polygon.Class = StringAt(*Row, 1);
				for (const TSharedPtr<FJsonValue>& Ring : *Rings)
				{
					TArray<FVector2d> Points;
					if (ReadLine(Ring, Points) && Points.Num() >= 3)
					{
						Polygon.Rings.Add(MoveTemp(Points));
					}
				}
				if (Polygon.Rings.Num() > 0)
				{
					Out.Land.Add(MoveTemp(Polygon));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (Root->TryGetArrayField(TEXT("buildings"), Buildings))
		{
			for (const TSharedPtr<FJsonValue>& RowValue : *Buildings)
			{
				const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
				if (!RowValue->TryGetArray(Row) || Row->Num() == 0)
				{
					continue;
				}
				FVector2d First;
				if (FirstVertex((*Row)[0], First))
				{
					Out.BuildingCentres.Add(First);
				}
				TArray<FVector2d> Ring;
				if (OuterRing((*Row)[0], Ring) && Ring.Num() >= 3)
				{
					FSimCruiseBuildingRow Building;
					Building.Ring = Ring;
					Building.HeightM = NumberAt(*Row, 2, 0.0);
					Building.MinHeightM = NumberAt(*Row, 3, 0.0);
					Building.Use = StringAt(*Row, 6);
					Out.Buildings.Add(MoveTemp(Building));
					Out.BuildingFootprints.Add(MoveTemp(Ring));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Pois = nullptr;
		if (Root->TryGetArrayField(TEXT("pois"), Pois))
		{
			for (const TSharedPtr<FJsonValue>& RowValue : *Pois)
			{
				const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
				if (!RowValue->TryGetArray(Row) || Row->Num() < 3) continue;
				FSimCruisePoi Poi;
				if (!ReadPoint((*Row)[0], Poi.Unit)) continue;
				Poi.Name = StringAt(*Row, 1).TrimStartAndEnd();
				Poi.Kind = StringAt(*Row, 2);
				if (!Poi.Name.IsEmpty()) Out.Pois.Add(MoveTemp(Poi));
			}
		}

		Root->TryGetStringField(TEXT("aerial_key"), Out.AerialKey);
		if (Out.AerialKey.IsEmpty())
		{
			Root->TryGetStringField(TEXT("aerial"), Out.AerialKey);
		}
		double Requests = 0.0;
		if (Root->TryGetNumberField(TEXT("requests"), Requests))
		{
			Out.Requests = static_cast<int32>(Requests);
		}
		return true;
	}

	bool LoadFile(const FString& Path, FSimCruiseTileData& Out, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("%s: cannot read"), *Path);
			return false;
		}
		if (!Parse(Text, Out, OutError))
		{
			OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
			return false;
		}
		Out.SourcePath = Path;
		return true;
	}

	FString RepositoryRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	}

	FString BundledTilePath(const FIntPoint& Tile)
	{
		return FPaths::Combine(RepositoryRoot(), TEXT("data"), TEXT("tiles"), FString::Printf(TEXT("%d_%d.json"), Tile.X, Tile.Y));
	}

	bool HasBundledTile(const FIntPoint& Tile)
	{
		return IFileManager::Get().FileExists(*BundledTilePath(Tile));
	}

	FString BundledAerialPath(const FString& AerialKey)
	{
		if (AerialKey.IsEmpty())
		{
			return FString();
		}
		const FString Path = FPaths::Combine(RepositoryRoot(), TEXT("data"), TEXT("tiles"), TEXT("aerial"), AerialKey);
		return IFileManager::Get().FileExists(*Path) ? Path : FString();
	}
}
