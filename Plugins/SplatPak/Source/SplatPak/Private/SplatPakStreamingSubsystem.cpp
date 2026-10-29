// SplatPakStreamingSubsystem.cpp

#include "SplatPakStreamingSubsystem.h"

#include "SplatPakComponent.h"
#include "SplatPakAsset.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

#include <StreamRuntime.h>

DEFINE_LOG_CATEGORY_STATIC(LogSplatPakStreaming, Log, All);

static TAutoConsoleVariable<float> CVarStreamingBudget(TEXT("r.SplatPak.Streaming.BandwidthMBps"), 25.0f,
	TEXT("The bandwidth every streaming SplatPak scan in a world shares, MB/s (25: about a 200 Mbit/s link).\n")
	TEXT("0 or less: no shared budget; each scan streams under its own Bandwidth MBps or Unthrottled setting."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarStreamingLog(TEXT("r.SplatPak.Streaming.Log"), 0,
	TEXT("1: a line a second per world with the delivery rate and each scan's share, pending requests and credit."),
	ECVF_Default);

namespace
{
	constexpr double kMiB = 1024.0 * 1024.0;
	constexpr float  kIdleAfterSeconds = 0.5f;  // Granted bandwidth, delivered nothing, asked for nothing: idle
	constexpr float  kProbeWeight = 0.05f;      // An idle scan's weight: enough to notice when it wants more
	constexpr float  kUrgentWeight = 4.0f;      // The view's own requests are waiting (demand or edge queue)
	constexpr float  kInViewWeight = 2.0f;      // Rendered in the last fifth of a second
	constexpr double kCreditSeconds = 0.25;     // A scan banks at most this long of its share (no bursts after a lull)

	// The requests the view has queued and not yet had delivered
	size_t PendingRequests(const Splat::StreamRuntime& R)
	{
		const size_t Demand = R.m_demandRequestQueue.size() > R.m_demandRequestHead ? R.m_demandRequestQueue.size() - R.m_demandRequestHead : 0;
		const size_t Edge = R.m_edgeQueue.size() > R.m_edgeHead ? R.m_edgeQueue.size() - R.m_edgeHead : 0;
		return Demand + Edge;
	}
}

float USplatPakStreamingSubsystem::GetBudgetMBps()
{
	return CVarStreamingBudget.GetValueOnGameThread();
}

TStatId USplatPakStreamingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USplatPakStreamingSubsystem, STATGROUP_Tickables);
}

void USplatPakStreamingSubsystem::Register(USplatPakComponent* Component)
{
	if (!Component || Entries.ContainsByPredicate([Component](const FEntry& E) { return E.Component.Get() == Component; })) return;
	FEntry& E = Entries.AddDefaulted_GetRef();
	E.Component = Component;
	UE_LOG(LogSplatPakStreaming, Log, TEXT("%s: %s joins the streaming budget (%d scans)"), *GetWorld()->GetName(), *Component->GetPathName(), Entries.Num());
}

void USplatPakStreamingSubsystem::Unregister(USplatPakComponent* Component)
{
	Entries.RemoveAll([Component](const FEntry& E) { return !E.Component.IsValid() || E.Component.Get() == Component; });
	if (Component) Component->ArbiterGrantMBps = -1.0f;
}

void USplatPakStreamingSubsystem::GetStreamingTotals(float& DeliveredMB, float& OutRateMBps, int32& BusyScans, int32& Scans) const
{
	DeliveredMB = (float)(TotalDelivered / kMiB);
	OutRateMBps = RateMBps;
	BusyScans = BusyCount;
	Scans = ActiveCount;
}

// Each tick: (1) what each runtime delivered since the last tick is charged to its credit; (2) each scan that still
// streams gets a weight from its priority and its need; (3) this tick's slice of the budget is split by those weights
// into the credits; (4) each component streams its next frame with its credit as its bandwidth, or holds while in debt.
void USplatPakStreamingSubsystem::Tick(float DeltaTime)
{
	// The budget accrues over the real frame time (up to a second); a grant is turned into the runtime's MB/s over its own
	// step, which the component caps at 0.1 s. Accruing over the capped step too delivered only 7.5 of 25 MB/s at 4 fps.
	const float Dt = FMath::Clamp(DeltaTime, 1.0e-4f, 0.1f);
	const float AccrueDt = FMath::Clamp(DeltaTime, 1.0e-4f, 1.0f);
	const float BudgetMBps = GetBudgetMBps();
	const bool bArbiter = BudgetMBps > 0.0f;
	Entries.RemoveAll([](const FEntry& E) { return !E.Component.IsValid(); });

	// (1) and (2)
	double SumWeight = 0.0, TickBytes = 0.0;
	BusyCount = 0;
	ActiveCount = 0;
	for (FEntry& E : Entries)
	{
		USplatPakComponent* C = E.Component.Get();
		const Splat::StreamRuntime* R = C->Runtime.Get();
		E.Weight = 0.0f;
		if (!R || C->bStreamHold || !C->bStreaming)
		{
			E.Credit = 0.0;
			E.LastBytes = -1.0;
			E.IdleSeconds = 0.0f;
			C->ArbiterGrantMBps = -1.0f;
			continue;
		}
		ActiveCount++;
		const double Bytes = R->m_simulatedBytesDelivered;
		double Delivered = 0.0;
		if (E.LastBytes >= 0.0)
		{
			// Evictions lower the counter; then the delivery counter says whether anything landed, at the mean block size
			Delivered = Bytes - E.LastBytes;
			const uint32 Landed = R->m_deliverySeqCounter - E.LastSeq;
			if (Delivered <= 0.0) Delivered = Landed > 0 && !R->m_allStreamChunkPtrs.empty() ? Landed * (R->m_totalStreamBytes / R->m_allStreamChunkPtrs.size()) : 0.0;
		}
		E.LastBytes = Bytes;
		E.LastSeq = R->m_deliverySeqCounter;
		E.Credit -= Delivered;
		E.DeliveredInWindow += Delivered;
		TickBytes += Delivered;

		const size_t Pending = PendingRequests(*R);
		const bool bGranted = C->ArbiterGrantMBps != 0.0f && !C->bArbiterPaused; // A scan held in debt is not idle, only waiting
		if (Delivered > 0.0 || Pending > 0) E.IdleSeconds = 0.0f;
		else if (bGranted || !bArbiter) E.IdleSeconds += AccrueDt;
		const bool bBusyScan = E.IdleSeconds < kIdleAfterSeconds;
		BusyCount += bBusyScan ? 1 : 0;

		const float Priority = FMath::Max(C->StreamPriority, 0.0f);
		E.Weight = bBusyScan
			? Priority * (Pending > 0 ? kUrgentWeight : 1.0f) * (C->WasRecentlyRendered(0.2f) ? kInViewWeight : 1.0f)
			: Priority * kProbeWeight;
		SumWeight += E.Weight;
	}
	TotalDelivered += TickBytes;
	WindowBytes += TickBytes;
	WindowSeconds += DeltaTime;

	// (3) and (4)
	const double SliceBytes = (double)BudgetMBps * kMiB * AccrueDt;
	for (FEntry& E : Entries)
	{
		USplatPakComponent* C = E.Component.Get();
		if (!C->Runtime.IsValid() || C->bStreamHold || !C->bStreaming) continue;
		if (!bArbiter)
		{
			E.Credit = 0.0;
			C->ArbiterGrantMBps = -1.0f; // Its own settings
			continue;
		}
		double Share = SumWeight > 0.0 ? SliceBytes * E.Weight / SumWeight : 0.0;
		if (!C->bUnthrottled) Share = FMath::Min(Share, (double)C->BandwidthMBps * kMiB * AccrueDt); // The component's own cap
		E.Credit = FMath::Min(E.Credit + Share, FMath::Max(Share, Share / AccrueDt * kCreditSeconds) + 1.0);
		C->bArbiterPaused = E.Credit <= 0.0;
		C->ArbiterGrantMBps = C->bArbiterPaused ? 0.0f : (float)(E.Credit / Dt / kMiB);
	}

	// "Fully streamed": every scan idle together, after something streamed
	if (BusyCount > 0)
	{
		if (!bBusy)
		{
			bBusy = true;
			BusySeconds = 0.0;
			BusyStartBytes = TotalDelivered - TickBytes;
			PeakRateMBps = 0.0;
		}
		BusySeconds += DeltaTime;
	}
	else if (bBusy && ActiveCount > 0)
	{
		bBusy = false;
		const double MB = (TotalDelivered - BusyStartBytes) / kMiB;
		// The idle wait itself is not streaming time
		const double Seconds = FMath::Max(BusySeconds - kIdleAfterSeconds, 1.0e-3);
		UE_LOG(LogSplatPakStreaming, Log, TEXT("%s: every scan settled: %d scans, %.1f MB in %.1f s (mean %.1f MB/s, peak %.1f MB/s), budget %s"),
			*GetWorld()->GetName(), ActiveCount, MB, Seconds, MB / Seconds, PeakRateMBps,
			bArbiter ? *FString::Printf(TEXT("%.1f MB/s shared"), BudgetMBps) : TEXT("off (each scan its own)"));
	}

	if (WindowSeconds >= 1.0f)
	{
		RateMBps = (float)(WindowBytes / kMiB / WindowSeconds);
		PeakRateMBps = FMath::Max(PeakRateMBps, (double)RateMBps);
		if (CVarStreamingLog.GetValueOnGameThread() > 0 && ActiveCount > 0)
		{
			FString Line;
			for (const FEntry& E : Entries)
			{
				const USplatPakComponent* C = E.Component.Get();
				const Splat::StreamRuntime* R = C->Runtime.Get();
				if (!R) continue;
				Line += FString::Printf(TEXT(" | %s %.1f MB/s w%.2f p%d%s"), C->SplatPakAsset ? *C->SplatPakAsset->GetName() : TEXT("-"),
					E.DeliveredInWindow / kMiB / WindowSeconds, E.Weight, (int32)PendingRequests(*R), E.IdleSeconds >= kIdleAfterSeconds ? TEXT(" idle") : TEXT(""));
			}
			UE_LOG(LogSplatPakStreaming, Log, TEXT("%s: %.1f MB/s (budget %.1f), %d of %d busy, %.1f MB in all%s"), *GetWorld()->GetName(),
				RateMBps, BudgetMBps, BusyCount, ActiveCount, TotalDelivered / kMiB, *Line);
		}
		for (FEntry& E : Entries) E.DeliveredInWindow = 0.0;
		WindowBytes = 0.0;
		WindowSeconds = 0.0f;
	}
}
