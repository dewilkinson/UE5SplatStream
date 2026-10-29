// SplatPakStreamingSubsystem.h
// One bandwidth budget for every streaming SplatPak scan in a world. Each USplatPakComponent keeps its own streaming
// runtime (residency depends on where its instance stands), but none of them streams under its own simulated link any
// more: every tick this subsystem measures what each runtime delivered, charges it against that runtime's credit, and
// splits the next slice of the world's budget (r.SplatPak.Streaming.BandwidthMBps) between the scans that still have
// something to load, weighted by need. A scan with nothing left to load gets only a probe's share, and the rest goes to
// the others. With the budget at 0 every scan streams under its own settings, as before.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SplatPakStreamingSubsystem.generated.h"

class USplatPakComponent;

UCLASS()
class SPLATPAK_API USplatPakStreamingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// The world budget, MB/s (r.SplatPak.Streaming.BandwidthMBps); 0 or less: no arbiter, each scan under its own settings
	static float GetBudgetMBps();
	static bool IsArbiterActive() { return GetBudgetMBps() > 0.0f; }

	void Register(USplatPakComponent* Component);
	void Unregister(USplatPakComponent* Component);

	// What has streamed since the world began, MB, and the delivery rate over the last second, MB/s
	UFUNCTION(BlueprintPure, Category = "SplatPak|Streaming")
	void GetStreamingTotals(float& DeliveredMB, float& RateMBps, int32& BusyScans, int32& Scans) const;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override;

private:
	struct FEntry
	{
		TWeakObjectPtr<USplatPakComponent> Component;
		double Credit = 0.0;          // Bytes this scan may still take; negative: what its last block overdrew
		double LastBytes = -1.0;      // The runtime's delivered-bytes counter at the last tick (-1: not read yet)
		uint32 LastSeq = 0;           // Its delivery counter, for a tick where evictions hid the deliveries
		float IdleSeconds = 0.0f;     // Granted bandwidth but delivered nothing and asked for nothing, for this long
		float Weight = 0.0f;          // This tick's share weight (for the log)
		double DeliveredInWindow = 0.0;
	};
	TArray<FEntry> Entries;

	double TotalDelivered = 0.0;      // Bytes, every scan, since the world began
	double WindowBytes = 0.0;         // Bytes in the current one-second window
	float WindowSeconds = 0.0f;
	float RateMBps = 0.0f;            // Over the last completed window
	int32 BusyCount = 0, ActiveCount = 0;

	// Settling: from the first busy tick until every scan has been idle together, for the "fully streamed" line
	bool bBusy = false;
	double BusySeconds = 0.0, BusyStartBytes = 0.0, PeakRateMBps = 0.0;
};
