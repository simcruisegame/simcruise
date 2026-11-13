// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/SimCruiseTuning.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ASimCruiseVehicle;
class SVerticalBox;

/**
 * The tuning menu (D-pad Right or F4, standing still): the car's setup in workshop units,
 * each row inside a safe range around the real car's, the body roll per g it gives, and a
 * reset to the real car. Up and down choose a row, left and right change it, X resets the
 * row, Y twice resets everything to the real car, A saves for this car and applies, B cancels.
 */
class SIMCRUISE_API SSimCruiseTuningMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruiseTuningMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ASimCruiseVehicle>, Vehicle)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Apply the working setup to the car and save it; false when the save failed. */
	bool Commit();
	enum class ETuningResult : uint8 { Open, Saved, SaveFailed, Cancelled };
	ETuningResult GetResult() const { return Result; }

	/** Change the selected row by Steps (tests). */
	void Nudge(int32 Steps);
	void Select(int32 Row) { Selected = FMath::Clamp(Row, 0, NumRows() - 1); }
	const FSimCruiseTuning& GetWorking() const { return Working; }

private:
	struct FRow
	{
		FText Label;
		double FSimCruiseTuning::* Field = nullptr;
		double Step = 1.0;
		FString Unit;
		int32 Decimals = 0;
	};
	int32 NumRows() const { return Rows.Num(); }
	FText RowText(int32 Index) const;
	FSlateColor RowColor(int32 Index) const;
	FText RollText() const;
	void Close(bool bSave);

	TWeakObjectPtr<ASimCruiseVehicle> Vehicle;
	FSimpleDelegate OnClose;
	FSimCruiseTuning Working;
	FSimCruiseTuning Stock;
	TArray<FRow> Rows;
	int32 Selected = 0;
	double StickHeldUntil = 0.0;
	bool bConfirmReset = false;
	FText Notice;
	ETuningResult Result = ETuningResult::Open;
};
