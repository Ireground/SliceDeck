#pragma once
// Slicing algorithms. Plain C++ (no JUCE) so they can be tested on their own.

#include <vector>
#include <cmath>
#include <algorithm>

namespace SliceEngine
{
    inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

    // Nearest zero crossing to 'pos' within +/- 3 ms. Returns pos if none found.
    inline int snapToZero (const float* mono, int len, double sr, int pos)
    {
        if (len < 2) return pos;
        pos = std::clamp (pos, 0, len - 1);
        const int range = std::max (1, (int) (sr * 0.003));
        for (int d = 0; d <= range; ++d)
        {
            for (int sign = -1; sign <= 1; sign += 2)
            {
                const int i = pos + sign * d;
                if (i <= 0 || i >= len) continue;
                const float a = mono[i - 1], b = mono[i];
                if ((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f))
                    return i;
            }
        }
        return pos;
    }

    // Transient detection between [start, end). sensitivity 0..1 (1 = more slices).
    // Returns slice start positions, always beginning with 'start'.
    inline std::vector<int> transients (const float* mono, int len, double sr,
                                        int start, int end, float sensitivity)
    {
        std::vector<int> points { start };
        start = std::clamp (start, 0, len);
        end   = std::clamp (end, start, len);
        const int hop = std::max (32, (int) (sr * 0.004));   // 4 ms analysis hop
        const int numHops = (end - start) / hop;
        if (numHops < 4) return points;

        // Energy per hop, weighted towards high frequencies (first difference)
        std::vector<float> db ((size_t) numHops);
        float maxDb = -200.0f;
        for (int h = 0; h < numHops; ++h)
        {
            double e = 0.0;
            const int s0 = start + h * hop;
            for (int i = s0; i < s0 + hop; ++i)
            {
                const float x = mono[i];
                const float d = i > 0 ? x - mono[i - 1] : x;
                e += (double) d * d + 0.25 * (double) x * x;
            }
            db[(size_t) h] = (float) (10.0 * std::log10 (e / hop + 1.0e-12));
            maxDb = std::max (maxDb, db[(size_t) h]);
        }

        sensitivity = std::clamp (sensitivity, 0.0f, 1.0f);
        const float riseNeeded = lerp (14.0f, 2.5f, sensitivity);       // dB jump
        const float floorDb    = maxDb - lerp (24.0f, 54.0f, sensitivity);
        const int   minGap     = std::max (1, (int) (lerp (0.150f, 0.035f, sensitivity) * sr / hop));

        int lastHop = 0;
        for (int h = 2; h < numHops; ++h)
        {
            if (db[(size_t) h] < floorDb) continue;
            // compare with average energy of up to 3 previous hops
            double prev = 0.0; int n = 0;
            for (int k = 1; k <= 3 && h - k >= 0; ++k) { prev += std::pow (10.0, db[(size_t) (h - k)] / 10.0); ++n; }
            const float prevDb = (float) (10.0 * std::log10 (prev / n + 1.0e-12));
            const float rise = db[(size_t) h] - prevDb;
            if (rise < riseNeeded || h - lastHop < minGap) continue;

            // keep the hop with the biggest rise among the next 2 hops
            int best = h; float bestRise = rise;
            for (int k = 1; k <= 2 && h + k < numHops; ++k)
            {
                const float r = db[(size_t) (h + k)] - prevDb;
                if (r > bestRise + 3.0f) { best = h + k; bestRise = r; }
            }

            // refine to the sample where the attack begins
            const int w0 = std::max (start, start + best * hop - hop);
            const int w1 = std::min (end, start + best * hop + 2 * hop);
            float peak = 0.0f;
            for (int i = w0; i < w1; ++i) peak = std::max (peak, std::abs (mono[i]));
            int pos = start + best * hop;
            for (int i = w0; i < w1; ++i)
                if (std::abs (mono[i]) > 0.2f * peak) { pos = i; break; }
            pos -= (int) (sr * 0.001);                       // 1 ms pre-roll
            pos = std::max (pos, points.back() + hop);
            if (pos >= end - hop) break;

            points.push_back (pos);
            lastHop = best;
            h = best;
        }
        return points;
    }

    inline std::vector<int> regions (int start, int end, int count)
    {
        std::vector<int> points;
        count = std::max (1, count);
        for (int i = 0; i < count; ++i)
            points.push_back (start + (int) ((long long) (end - start) * i / count));
        return points;
    }

    // Beat slicing: assumes the sample plays at the project tempo.
    inline std::vector<int> beats (double sr, int start, int end, double bpm, double beatsPerSlice)
    {
        std::vector<int> points;
        const double step = std::max (16.0, beatsPerSlice * 60.0 / std::max (20.0, bpm) * sr);
        for (double p = start; p < end - 16 && points.size() < 128; p += step)
            points.push_back ((int) p);
        if (points.empty()) points.push_back (start);
        return points;
    }
}
