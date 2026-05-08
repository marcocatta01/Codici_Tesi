#include "FMUhandling/FMUInputDataStructures.h"
#include "Algo/Sort.h"
#include "Engine/Engine.h"

// FFMUInputTimeseries implementation

bool FFMUInputTimeseries::GetValueAtTime(double time, double &outValue, EFMUInputInterpolation interpolationMethod) const
{
    if (timeValues.IsEmpty())
    {
        return false;
    }

    // Handle looping
    double evaluationTime = time;
    if (bLooping && loopDuration > 0.0)
    {
        evaluationTime = FMath::Fmod(time, loopDuration);
        if (evaluationTime < 0.0)
        {
            evaluationTime += loopDuration;
        }
    }

    // Find interval [leftIndex, rightIndex] with time[left] <= evaluationTime < time[right]
    int32 leftIndex = -1;
    int32 rightIndex = -1;

    for (int32 i = 0; i < timeValues.Num(); ++i)
    {
        if (timeValues[i].time <= evaluationTime)
        {
            leftIndex = i;
        }
        else
        {
            rightIndex = i;
            break;
        }
    }

    // Edge cases
    if (leftIndex == -1)
    {
        outValue = timeValues[0].value;
        return true;
    }
    if (rightIndex == -1)
    {
        outValue = timeValues[leftIndex].value;
        return true;
    }

    // Exact match
    if (FMath::IsNearlyEqual(timeValues[leftIndex].time, evaluationTime))
    {
        outValue = timeValues[leftIndex].value;
        return true;
    }

    // Interpolation
    const double tL = timeValues[leftIndex].time;
    const double vL = timeValues[leftIndex].value;
    const double tR = timeValues[rightIndex].time;
    const double vR = timeValues[rightIndex].value;

    switch (interpolationMethod)
    {
    case EFMUInputInterpolation::None:
        outValue = vL;
        break;

    case EFMUInputInterpolation::Linear:
        outValue = LinearInterpolate(evaluationTime, tL, vL, tR, vR);
        break;

    case EFMUInputInterpolation::Cubic:
    {
        const int32 N = timeValues.Num();
        // If too few points, fall back to linear
        if (N < 3)
        {
            outValue = LinearInterpolate(evaluationTime, tL, vL, tR, vR);
            break;
        }

        // Determine prev and next indices (Catmull-Rom style)
        int32 prevIndex = leftIndex - 1;
        int32 nextIndex = rightIndex + 1;

        // Handle boundaries
        if (prevIndex < 0)
            prevIndex = leftIndex; // duplicate (secant slope)
        if (nextIndex >= N)
            nextIndex = rightIndex; // duplicate (secant slope)

        // Tangents m_left, m_right
        double m_left;
        double m_right;

        // m_left: (v_right - v_prev)/(t_right - t_prev)
        {
            const double tPrev = timeValues[prevIndex].time;
            const double vPrev = timeValues[prevIndex].value;
            const double denom = (tR - tPrev);
            if (FMath::IsNearlyZero(denom))
            {
                m_left = (vR - vL) / (tR - tL);
            }
            else
            {
                m_left = (vR - vPrev) / denom;
            }
        }

        // m_right: (v_next - v_left)/(t_next - t_left)
        {
            const double tNext = timeValues[nextIndex].time;
            const double vNext = timeValues[nextIndex].value;
            const double denom = (tNext - tL);
            if (FMath::IsNearlyZero(denom))
            {
                m_right = (vR - vL) / (tR - tL);
            }
            else
            {
                m_right = (vNext - vL) / denom;
            }
        }

        double cubicValue = CubicHermite(evaluationTime, tL, vL, m_left, tR, vR, m_right);

        if (bClampCubicToSegment)
        {
            const double segMin = FMath::Min(vL, vR);
            const double segMax = FMath::Max(vL, vR);
            cubicValue = FMath::Clamp(cubicValue, segMin, segMax);
        }

        outValue = cubicValue;
        break;
    }

    default:
        outValue = vL;
        break;
    }

    return true;
}

void FFMUInputTimeseries::AddTimeValue(double time, double value)
{
    timeValues.Add(FFMUInputTimeValue(time, value));
}

void FFMUInputTimeseries::Clear()
{
    timeValues.Empty();
}

void FFMUInputTimeseries::Sort()
{
    Algo::Sort(timeValues, [](const FFMUInputTimeValue &A, const FFMUInputTimeValue &B)
               { return A.time < B.time; });
}

bool FFMUInputTimeseries::GetTimeRange(double &outStartTime, double &outEndTime) const
{
    if (timeValues.IsEmpty())
    {
        return false;
    }

    outStartTime = timeValues[0].time;
    outEndTime = timeValues[0].time;

    for (const FFMUInputTimeValue &timeValue : timeValues)
    {
        outStartTime = FMath::Min(outStartTime, timeValue.time);
        outEndTime = FMath::Max(outEndTime, timeValue.time);
    }

    return true;
}

double FFMUInputTimeseries::LinearInterpolate(double t, double t1, double v1, double t2, double v2) const
{
    if (FMath::IsNearlyEqual(t1, t2))
    {
        return v1;
    }

    double alpha = (t - t1) / (t2 - t1);
    return FMath::Lerp(v1, v2, alpha);
}

double FFMUInputTimeseries::CubicHermite(double t,
                                         double t0, double v0, double m0,
                                         double t1, double v1, double m1) const
{
    if (FMath::IsNearlyEqual(t0, t1))
    {
        return v0;
    }

    const double h = (t1 - t0);
    const double u = (t - t0) / h; // normalized position in segment

    // Basis functions
    const double u2 = u * u;
    const double u3 = u2 * u;

    const double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
    const double h10 = u3 - 2.0 * u2 + u;
    const double h01 = -2.0 * u3 + 3.0 * u2;
    const double h11 = u3 - u2;

    return h00 * v0 + h10 * h * m0 + h01 * v1 + h11 * h * m1;
}

// FFMUInputSource implementation

bool FFMUInputSource::GetValue(double currentTime, double &outValue, EFMUInputInterpolation interpolationMethod) const
{
    if (!bEnabled || !IsValid(currentTime))
    {
        return false;
    }

    switch (sourceType)
    {
    case EFMUInputSourceType::Static:
        outValue = staticValue;
        return true;

    case EFMUInputSourceType::Timeseries:
        return timeseries.GetValueAtTime(currentTime, outValue, interpolationMethod);

    case EFMUInputSourceType::WebSocket:
    case EFMUInputSourceType::Manual:
        outValue = staticValue;
        return true;

    case EFMUInputSourceType::GameController:
    case EFMUInputSourceType::VRHeadset:
        outValue = staticValue;
        return true;

    default:
        return false;
    }
}

void FFMUInputSource::UpdateValue(double currentTime, double newValue)
{
    staticValue = newValue;
    lastUpdateTime = currentTime;
}

bool FFMUInputSource::IsValid(double currentTime) const
{
    if (!bEnabled)
    {
        return false;
    }

    if (validityDuration > 0.0)
    {
        double timeSinceUpdate = currentTime - lastUpdateTime;
        if (timeSinceUpdate > validityDuration)
        {
            return false;
        }
    }

    return true;
}

// FFMUInputVariableConfig implementation

bool FFMUInputVariableConfig::GetCurrentValue(double currentTime, double &outValue) const
{
    const FFMUInputSource *bestSource = GetBestInputSource(currentTime);
    if (!bestSource)
    {
        outValue = defaultValue;
        return true;
    }

    double rawValue;
    // Passa l'interpolazione specifica della variabile alla sorgente
    if (!bestSource->GetValue(currentTime, rawValue, interpolationMethod))
    {
        outValue = defaultValue;
        return true;
    }

    double convertedValue = rawValue * unitConversionFactor;

    if (bClampToRange)
    {
        convertedValue = FMath::Clamp(convertedValue, minValue, maxValue);
    }

    outValue = convertedValue;
    return true;
}

void FFMUInputVariableConfig::AddInputSource(const FFMUInputSource &inputSource)
{
    RemoveInputSource(inputSource.sourceId);
    inputSources.Add(inputSource);
}

void FFMUInputVariableConfig::RemoveInputSource(const FString &sourceId)
{
    inputSources.RemoveAll([&sourceId](const FFMUInputSource &source)
                           { return source.sourceId == sourceId; });
}

FFMUInputSource *FFMUInputVariableConfig::FindInputSource(const FString &sourceId)
{
    return inputSources.FindByPredicate([&sourceId](const FFMUInputSource &source)
                                        { return source.sourceId == sourceId; });
}

const FFMUInputSource *FFMUInputVariableConfig::GetBestInputSource(double currentTime) const
{
    const FFMUInputSource *bestSource = nullptr;
    EFMUInputPriority highestPriority = EFMUInputPriority::Low;

    for (const FFMUInputSource &source : inputSources)
    {
        if (source.IsValid(currentTime))
        {
            if (bestSource == nullptr || source.priority > highestPriority)
            {
                bestSource = &source;
                highestPriority = source.priority;
            }
        }
    }

    return bestSource;
}

// FFMUInputConfiguration implementation

FFMUInputVariableConfig *FFMUInputConfiguration::FindInputVariable(const FString &variableName)
{
    return inputVariables.FindByPredicate([&variableName](const FFMUInputVariableConfig &config)
                                          { return config.variableName == variableName; });
}

const FFMUInputVariableConfig *FFMUInputConfiguration::FindInputVariable(const FString &variableName) const
{
    return inputVariables.FindByPredicate([&variableName](const FFMUInputVariableConfig &config)
                                          { return config.variableName == variableName; });
}

void FFMUInputConfiguration::SetInputVariable(const FFMUInputVariableConfig &variableConfig)
{
    RemoveInputVariable(variableConfig.variableName);
    inputVariables.Add(variableConfig);
}

void FFMUInputConfiguration::RemoveInputVariable(const FString &variableName)
{
    inputVariables.RemoveAll([&variableName](const FFMUInputVariableConfig &config)
                             { return config.variableName == variableName; });
}