// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseTileManager.h"

#include "World/SimCruiseTileBuilder.h"

namespace
{
	double GapToTile(const FVector2d& P, const FIntPoint& Tile)
	{
		const FVector2d Lo(Tile.X, Tile.Y), Hi(Tile.X + 1.0, Tile.Y + 1.0);
		const FVector2d Clamped(FMath::Clamp(P.X, Lo.X, Hi.X), FMath::Clamp(P.Y, Lo.Y, Hi.Y));
		return FVector2d::Distance(P, Clamped);
	}
}

uint32 FSimCruiseTileManager::Reset()
{
	++Generation;
	Resident.Reset();
	InFlight.Reset();
	RetryAt.Reset();
	bHasFocus = false;
	return Generation;
}

void FSimCruiseTileManager::SetFocus(const FVector2d& FocusTileCoord, const FVector2d& PredictedTileCoord)
{
	Focus = FocusTileCoord;
	Predicted = PredictedTileCoord;
	FocusTile = FIntPoint(FMath::FloorToInt(Focus.X), FMath::FloorToInt(Focus.Y));
	bHasFocus = true;
}

bool FSimCruiseTileManager::IsWanted(const FIntPoint& Tile, int32 Slack) const
{
	if (!bHasFocus) return false;
	const int32 Reach = RingRadius + Slack;
	return FMath::Abs(Tile.X - FocusTile.X) <= Reach && FMath::Abs(Tile.Y - FocusTile.Y) <= Reach;
}

TArray<FSimCruiseTileJobTicket> FSimCruiseTileManager::TakeRequests(double NowSeconds)
{
	TArray<FSimCruiseTileJobTicket> Out;
	if (!bHasFocus) return Out;
	struct FCandidate { FIntPoint Tile; double Score; bool bRoute; };
	TArray<FCandidate> Candidates;
	// The route: the tiles the line from the car to where it will be (the streamer's look-ahead,
	// speed x (2 x load time + 4 s)) crosses, out to the ring plus one (IsWanted's slack, so the
	// tile is not evicted again). At highway speed the ring alone starts the tile after next only
	// when the car enters a tile: too late when a tile takes longer to load than to cross.
	TSet<FIntPoint> Route;
	Route.Add(FocusTile);
	const double Len = FVector2d::Distance(Focus, Predicted);
	{
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Len / 0.05), 0, 128);
		for (int32 k = 1; k <= Steps; ++k)
		{
			const FVector2d P = FMath::Lerp(Focus, Predicted, static_cast<double>(k) / Steps);
			const FIntPoint Tile(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y));
			if (FMath::Max(FMath::Abs(Tile.X - FocusTile.X), FMath::Abs(Tile.Y - FocusTile.Y)) > RingRadius + 1) break;
			Route.Add(Tile);
		}
	}
	auto Usable = [&](const FIntPoint& Tile)
	{
		if (Resident.Contains(Tile) || InFlight.Contains(Tile)) return false;
		const double* When = RetryAt.Find(Tile);
		return !(When && *When > NowSeconds);
	};
	for (const FIntPoint& Tile : Route)
	{
		// Route tiles by arrival: the gap from the car along the line.
		if (Usable(Tile)) Candidates.Add({ Tile, GapToTile(Focus, Tile), true });
	}
	for (int32 dy = -RingRadius; dy <= RingRadius; ++dy)
	for (int32 dx = -RingRadius; dx <= RingRadius; ++dx)
	{
		const FIntPoint Tile = FocusTile + FIntPoint(dx, dy);
		if (Route.Contains(Tile) || !Usable(Tile)) continue;
		// Drive.gd: 40 % the gap from the car, 60 % from where it will be.
		Candidates.Add({ Tile, GapToTile(Focus, Tile) * 0.4 + GapToTile(Predicted, Tile) * 0.6, false });
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.bRoute != B.bRoute ? A.bRoute : A.Score < B.Score; });
	// While moving, tiles off the route may hold all but one load slot: the road ahead always
	// has one (SimCruise.World.TileManager.HighwayLookAhead: at 130 km/h with 8 s loads and two
	// slots, every tile is ready 4 s before the car on any heading; the side tiles follow).
	const bool bMoving = Len > 0.05;
	int32 OffRoute = 0;
	for (const TPair<FIntPoint, uint64>& F : InFlight) OffRoute += Route.Contains(F.Key) ? 0 : 1;
	for (const FCandidate& C : Candidates)
	{
		if (InFlight.Num() >= MaxInFlight) break;
		if (bMoving && !C.bRoute && MaxInFlight > 1 && OffRoute >= MaxInFlight - 1) continue;
		// Edge neighbours off the route never build at the same time: the later one reads the
		// earlier one's seam contract (FSimCruiseSeamEnd), so a seam has one section. Route
		// tiles may: an uncontracted seam end is pinned to an absolute height from the shared
		// ground (52e9779, SimCruise.World.SlopedSeam: the sides meet within 1 cm).
		bool bNeighbourBusy = false;
		for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
		{
			bNeighbourBusy |= InFlight.Contains(C.Tile + Step);
		}
		if (bNeighbourBusy && !C.bRoute) continue;
		FSimCruiseTileJobTicket Ticket;
		Ticket.Tile = C.Tile;
		Ticket.Generation = Generation;
		Ticket.Serial = NextSerial++;
		InFlight.Add(C.Tile, Ticket.Serial);
		Out.Add(Ticket);
		OffRoute += C.bRoute ? 0 : 1;
	}
	return Out;
}

TArray<FIntPoint> FSimCruiseTileManager::TakeEvictions()
{
	TArray<FIntPoint> Out;
	for (const FIntPoint& Tile : Resident)
	{
		if (!IsWanted(Tile, 1)) Out.Add(Tile);
	}
	// Hard bound: past MaxResident, the farthest go first.
	if (Resident.Num() - Out.Num() > MaxResident())
	{
		TArray<FIntPoint> Kept;
		for (const FIntPoint& Tile : Resident) if (!Out.Contains(Tile)) Kept.Add(Tile);
		Kept.Sort([this](const FIntPoint& A, const FIntPoint& B) { return (A - FocusTile).SizeSquared() > (B - FocusTile).SizeSquared(); });
		for (int32 i = 0; i < Kept.Num() - MaxResident(); ++i) Out.Add(Kept[i]);
	}
	for (const FIntPoint& Tile : Out) Resident.Remove(Tile);
	return Out;
}

bool FSimCruiseTileManager::IsCurrent(const FSimCruiseTileJobTicket& Ticket) const
{
	if (Ticket.Generation != Generation) return false;
	const uint64* Serial = InFlight.Find(Ticket.Tile);
	if (!Serial || *Serial != Ticket.Serial) return false;
	return !Resident.Contains(Ticket.Tile) && IsWanted(Ticket.Tile, 1);
}

bool FSimCruiseTileManager::Complete(const FSimCruiseTileJobTicket& Ticket, bool bSucceeded, double NowSeconds)
{
	const bool bCurrent = IsCurrent(Ticket);
	if (Ticket.Generation == Generation)
	{
		const uint64* Serial = InFlight.Find(Ticket.Tile);
		if (Serial && *Serial == Ticket.Serial) InFlight.Remove(Ticket.Tile);
	}
	if (!bCurrent) return false;
	if (!bSucceeded)
	{
		RetryAt.Add(Ticket.Tile, NowSeconds + RetryDelaySeconds);
		return false;
	}
	RetryAt.Remove(Ticket.Tile);
	Resident.Add(Ticket.Tile);
	return true;
}

namespace SimCruiseFallbackGround
{
	double HeightM(const FIntPoint& Tile, TFunctionRef<const FSimCruiseTileBuild*(const FIntPoint&)> Find, double DefaultM, int32* OutNeighbours)
	{
		double Sum = 0.0;
		int32 Num = 0, Neighbours = 0;
		for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
		{
			const FSimCruiseTileBuild* B = Find(Tile + Step);
			if (!B || B->TerrainN <= 0 || B->TerrainHeights.Num() != FMath::Square(B->TerrainN + 1)) continue;
			const int32 N = B->TerrainN;
			++Neighbours;
			// The neighbour's edge that touches this tile (x east, y south; row j, column i).
			for (int32 k = 0; k <= N; ++k)
			{
				const int32 I = Step.X == 1 ? 0 : Step.X == -1 ? N : k;
				const int32 J = Step.Y == 1 ? 0 : Step.Y == -1 ? N : k;
				Sum += B->TerrainHeights[J * (N + 1) + I];
				++Num;
			}
		}
		if (OutNeighbours) *OutNeighbours = Neighbours;
		return Num > 0 ? Sum / Num : DefaultM;
	}
}
