// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseTileManager.h"

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
	struct FCandidate { FIntPoint Tile; double Score; };
	TArray<FCandidate> Candidates;
	for (int32 dy = -RingRadius; dy <= RingRadius; ++dy)
	for (int32 dx = -RingRadius; dx <= RingRadius; ++dx)
	{
		const FIntPoint Tile = FocusTile + FIntPoint(dx, dy);
		if (Resident.Contains(Tile) || InFlight.Contains(Tile)) continue;
		if (const double* When = RetryAt.Find(Tile); When && *When > NowSeconds) continue;
		// Drive.gd: 40 % the gap from the car, 60 % from where it will be in 5 s.
		const double Score = GapToTile(Focus, Tile) * 0.4 + GapToTile(Predicted, Tile) * 0.6;
		Candidates.Add({ Tile, Score });
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score < B.Score; });
	for (const FCandidate& C : Candidates)
	{
		if (InFlight.Num() >= MaxInFlight) break;
		// Edge neighbours never build at the same time: the later one reads the
		// earlier one's seam contract (FSimCruiseSeamEnd), so a seam has one section.
		bool bNeighbourBusy = false;
		for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
		{
			bNeighbourBusy |= InFlight.Contains(C.Tile + Step);
		}
		if (bNeighbourBusy) continue;
		FSimCruiseTileJobTicket Ticket;
		Ticket.Tile = C.Tile;
		Ticket.Generation = Generation;
		Ticket.Serial = NextSerial++;
		InFlight.Add(C.Tile, Ticket.Serial);
		Out.Add(Ticket);
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
