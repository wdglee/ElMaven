#include <omp.h>

#include <boost/signals2.hpp>
#include <boost/bind.hpp>

#include "massslicer.h"
#include "Compound.h"
#include "EIC.h"
#include "mavenparameters.h"
#include "mzSample.h"
#include "datastructures/adduct.h"
#include "datastructures/isotope.h"
#include "datastructures/mzSlice.h"
#include "masscutofftype.h"
#include "mzUtils.h"
#include "Matrix.h"
#include "peakdetector.h"
#include "Scan.h"

using namespace mzUtils;

MassSlicer::MassSlicer(MavenParameters* mp) : _mavenParameters(mp)
{
    _samples = _mavenParameters->samples;
}

MassSlicer::~MassSlicer()
{
    delete_all(slices);
}

void MassSlicer::sendSignal(const string& progressText,
                unsigned int completed_samples,
                int total_samples)
{
    _mavenParameters->sig(progressText, completed_samples, total_samples);
}

void MassSlicer::clearSlices()
{
    if (slices.size() > 0) {
        delete_all(slices);
        slices.clear();
    }
}

void MassSlicer::generateCompoundSlices(vector<Compound*> compounds,
                                        bool clearPrevious)
{
    if (clearPrevious)
        clearSlices();
    if (_samples.empty())
        return;

    Adduct* adduct = nullptr;
    for (auto parentAdduct : _mavenParameters->getDefaultAdductList()) {
        if (SIGN(parentAdduct->getCharge())
            == SIGN(_mavenParameters->ionizationMode)) {
            adduct = parentAdduct;
        }
    }

    if (adduct == nullptr)
        return;

    for(auto compound : compounds) {
        if (_mavenParameters->stop) {
            delete_all(slices);
            break;
        }

        if (compound == nullptr)
            continue;

        if (compound->type() == Compound::Type::MRM) {
            mzSlice* slice = new mzSlice();
            slice->compound = compound;
            slice->setSRMId();
            slice->calculateRTMinMax(_mavenParameters->matchRtFlag,
                                     _mavenParameters->compoundRTWindow);
            slices.push_back(slice);
        } else {
            mzSlice* slice = new mzSlice;
            slice->compound = compound;
            slice->calculateMzMinMax(_mavenParameters->compoundMassCutoffWindow,
                                     _mavenParameters->getCharge());

            // we intentionally set the adduct after calculating min/max m/z
            // so that the global charge is used for adjusting compound's mass
            slice->adduct = adduct;

            slice->calculateRTMinMax(_mavenParameters->matchRtFlag,
                                     _mavenParameters->compoundRTWindow);
            slices.push_back(slice);
        }
    }
}

void MassSlicer::generateIsotopeSlices(vector<Compound*> compounds,
                                       bool sliceBarplotIsotopes,
                                       bool clearPrevious)
{
    if (clearPrevious)
        clearSlices();
    if (_samples.empty())
        return;

    for (auto compound : compounds) {
        if (_mavenParameters->stop) {
            clearSlices();
            break;
        }

        if (compound == nullptr)
            continue;
        if (compound->formula().empty())
            continue;

        string formula = compound->formula();
        int charge = _mavenParameters->getCharge(compound);

        Adduct* adduct = nullptr;
        for (auto parentAdduct : _mavenParameters->getDefaultAdductList()) {
            if (SIGN(parentAdduct->getCharge()) == SIGN(charge)) {
                adduct = parentAdduct;
            }
        }
        if (adduct == nullptr)
            continue;

        bool findC13 = _mavenParameters->C13Labeled_BPE;
        bool findN15 = _mavenParameters->N15Labeled_BPE;
        bool findS34 = _mavenParameters->S34Labeled_BPE;
        bool findD2 = _mavenParameters->D2Labeled_BPE;
        if (sliceBarplotIsotopes) {
            findC13 = _mavenParameters->C13Labeled_Barplot;
            findN15 = _mavenParameters->N15Labeled_Barplot;
            findS34 = _mavenParameters->S34Labeled_Barplot;
            findD2 = _mavenParameters->D2Labeled_Barplot;
        }

        vector<Isotope> massList =
            MassCalculator::computeIsotopes(formula,
                                            charge,
                                            findC13,
                                            findN15,
                                            findS34,
                                            findD2,
                                            adduct);
        for (auto isotope : massList) {
            mzSlice* slice = new mzSlice;
            slice->compound = compound;
            slice->isotope = isotope;
            slice->calculateMzMinMax(_mavenParameters->compoundMassCutoffWindow,
                                     _mavenParameters->getCharge());

            // we intentionally set the adduct after calculating min/max m/z
            // so that the global charge is used for adjusting compound's mass
            slice->adduct = adduct;

            slice->calculateRTMinMax(_mavenParameters->matchRtFlag,
                                     _mavenParameters->compoundRTWindow);
            slices.push_back(slice);
        }
    }
}

void MassSlicer::generateAdductSlices(vector<Compound*> compounds,
                                      bool ignoreParentAdducts,
                                      bool clearPrevious)
{
    if (clearPrevious)
        clearSlices();
    if (_samples.empty())
        return;

    MassCalculator massCalc;
    for (auto compound : compounds) {
        if (_mavenParameters->stop) {
            clearSlices();
            break;
        }

        for (auto adduct : _mavenParameters->getChosenAdductList()) {
            if (adduct->isParent() && ignoreParentAdducts)
                continue;

            float neutralMass = compound->neutralMass();
            if (!compound->formula().empty())
                neutralMass = massCalc.computeNeutralMass(compound->formula());

            // we have to have a neutral mass for non-parent adducts
            if (neutralMass <= 0.0f && !adduct->isParent())
                continue;

            mzSlice* slice = new mzSlice;
            slice->compound = compound;

            // depending on whether the adduct is of a parent type or not, we
            // use the global charge or the adduct's charge to calculate m/z
            if (adduct->isParent()) {
                slice->calculateMzMinMax(
                    _mavenParameters->compoundMassCutoffWindow,
                    _mavenParameters->getCharge());
                slice->adduct = adduct;
            } else {
                slice->adduct = adduct;
                slice->calculateMzMinMax(_mavenParameters->compoundMassCutoffWindow,
                                         adduct->getCharge());
            }

            slice->calculateRTMinMax(_mavenParameters->matchRtFlag,
                                     _mavenParameters->compoundRTWindow);
            slices.push_back(slice);
        }
    }
}

void MassSlicer::findFeatureSlices(bool clearPrevious)
{
    if (clearPrevious)
        clearSlices();

    MassCutoff* massCutoff = _mavenParameters->massCutoffMerge;
    float minFeatureRt = _mavenParameters->minRt;
    float maxFeatureRt = _mavenParameters->maxRt;
    float minFeatureMz = _mavenParameters->minMz;
    float maxFeatureMz = _mavenParameters->maxMz;
    float minFeatureIntensity = _mavenParameters->minIntensity;
    float maxFeatureIntensity = _mavenParameters->maxIntensity;

    int totalScans = 0;
    int currentScans = 0;

    // Calculate the total number of scans
    for (auto s : _samples)
        totalScans += s->scans.size();

    // Calculating the rt window using average distance between RTs and
    // mutiplying it with rtStep (default 2.0)
    float rtWindow = 2.0f;
    int rtStep = _mavenParameters->rtStepSize;
    if (_samples.size() > 0 and rtStep > 0) {
        rtWindow = accumulate(begin(_samples),
                              end(_samples),
                              0.0f,
                              [rtStep](float sum, mzSample* sample) {
                                  return sum + (sample->getAverageFullScanTime()
                                                * rtStep);
                              }) / static_cast<float>(_samples.size());
    }
    cerr << "RT window used: " << rtWindow << endl;

    sendSignal("Status", 0 , 1);

    // looping over every sample
    for (unsigned int i = 0; i < _samples.size(); i++) {
        // Check if peak detection has been cancelled by the user
        if (_mavenParameters->stop) {
            clearSlices();
            break;
        }

        // updating progress on samples
        if (_mavenParameters->showProgressFlag) {
            string progressText = "Processing "
                                  + to_string(i + 1)
                                  + " out of "
                                  + to_string(_mavenParameters->samples.size())
                                  + " sample(s)…";
            sendSignal(progressText, currentScans, totalScans);
        }

        // #pragma omp cancel for
        // for loop for iterating over every scan of a sample
        for (auto scan : _samples[i]->scans) {
            // Check if Peak detection has been cancelled by the user
            if (_mavenParameters->stop) {
                clearSlices();
                break;
            }

            currentScans++;

            if (scan->mslevel != 1)
                continue;

            // Checking if RT is in the given min to max RT range
            if (!isBetweenInclusive(scan->rt, minFeatureRt, maxFeatureRt))
                continue;

            float rt = scan->rt;

            for (unsigned int k = 0; k < scan->nobs(); k++) {
                float mz = scan->mz[k];
                float intensity = scan->intensity[k];

                // Checking if mz, intensity are within specified ranges
                if (!isBetweenInclusive(mz, minFeatureMz, maxFeatureMz))
                    continue;

                if (!isBetweenInclusive(intensity,
                                        minFeatureIntensity,
                                        maxFeatureIntensity)) {
                    continue;
                }

                // create new slice with the given bounds
                float cutoff = massCutoff->massCutoffValue(mz);
                mzSlice* s = new mzSlice(mz - cutoff,
                                         mz + cutoff,
                                         rt - rtWindow,
                                         rt + rtWindow);
                s->ionCount = intensity;
                s->rt = scan->rt;
                s->mz = mz;
                slices.push_back(s);
            }

            // progress update 
            if (_mavenParameters->showProgressFlag ) {
                string progressText = "Processing "
                                      + to_string(i + 1)
                                      + " out of "
                                      + to_string(_mavenParameters->samples.size())
                                      + " sample(s)…\n"
                                      + to_string(slices.size())
                                      + " slices created";
                sendSignal(progressText,currentScans,totalScans);
            }
        }
    }

    cerr << "Found " << slices.size() << " slices" << endl;

    // before reduction sort by mz first then by rt
    sort(begin(slices),
         end(slices),
         [](const mzSlice* slice, const mzSlice* compSlice) {
             if (slice->mz == compSlice->mz) {
                 return slice->rt < compSlice->rt;
             }
             return slice->mz < compSlice->mz;
         });
    _reduceSlices(massCutoff);

    cerr << "Reduced to " << slices.size() << " slices" << endl;

    sort(slices.begin(), slices.end(), mzSlice::compMz);
    _mergeSlices(massCutoff, rtWindow);
    _adjustSlices(massCutoff);

    cerr << "After final merging and adjustments, "
         << slices.size()
         << " slices remain"
         << endl;
    sendSignal("Mass slicing done.", 1 , 1);
}

void MassSlicer::_reduceSlices(MassCutoff* massCutoff)
{
    for (auto first = begin(slices); first != end(slices); ++first) {
        if (_mavenParameters->stop) {
            clearSlices();
            break;
        }

        auto firstSlice = *first;
        if (mzUtils::almostEqual(firstSlice->ionCount, -1.0f))
            continue;

        // we will use this to terminate large shifts in slices, where they
        // might end up losing their original information completely
        auto originalMax = firstSlice->mzmax;

        for (auto second = next(first); second != end(slices); ++second) {
            auto secondSlice = *second;

            // stop iterating if the rest of the slices are too far
            if (originalMax < secondSlice->mzmin
                || firstSlice->mzmax < secondSlice->mzmin)
                break;

            if (mzUtils::almostEqual(secondSlice->ionCount, -1.0f))
                continue;

            // check if center of one of the slices lies in the other
            if ((firstSlice->mz > secondSlice->mzmin
                 && firstSlice->mz < secondSlice->mzmax
                 && firstSlice->rt > secondSlice->rtmin
                 && firstSlice->rt < secondSlice->rtmax)
                ||
                (secondSlice->mz > firstSlice->mzmin
                 && secondSlice->mz < firstSlice->mzmax
                 && secondSlice->rt > firstSlice->rtmin
                 && secondSlice->rt < firstSlice->rtmax)) {
                firstSlice->ionCount = std::max(firstSlice->ionCount,
                                                secondSlice->ionCount);
                firstSlice->rtmax = std::max(firstSlice->rtmax,
                                             secondSlice->rtmax);
                firstSlice->rtmin = std::min(firstSlice->rtmin,
                                             secondSlice->rtmin);
                firstSlice->mzmax = std::max(firstSlice->mzmax,
                                             secondSlice->mzmax);
                firstSlice->mzmin = std::min(firstSlice->mzmin,
                                             secondSlice->mzmin);

                firstSlice->mz = (firstSlice->mzmin + firstSlice->mzmax) / 2.0f;
                firstSlice->rt = (firstSlice->rtmin + firstSlice->rtmax) / 2.0f;
                float cutoff = massCutoff->massCutoffValue(firstSlice->mz);

                // make sure that mz window does not get out of control
                if (firstSlice->mzmin < firstSlice->mz - cutoff)
                    firstSlice->mzmin =  firstSlice->mz - cutoff;
                if (firstSlice->mzmax > firstSlice->mz + cutoff)
                    firstSlice->mzmax =  firstSlice->mz + cutoff;

                // recalculate center mz in case bounds changed
                firstSlice->mz = (firstSlice->mzmin + firstSlice->mzmax) / 2.0f;

                // flag this slice as already merged, and ignore henceforth
                secondSlice->ionCount = -1.0f;
            }
        }
        sendSignal("Reducing redundant slices…",
                   first - begin(slices),
                   slices.size());
    }

    // remove merged slices
    vector<size_t> indexesToErase;
    for (size_t i = 0; i < slices.size(); ++i) {
        auto slice = slices[i];
        if (slice->ionCount == -1.0f) {
            delete slice;
            indexesToErase.push_back(i);
        }
    }
    mzUtils::eraseIndexes(slices, indexesToErase);
}

void MassSlicer::_mergeSlices(const MassCutoff* massCutoff,
                              const float rtTolerance)
{
    // lambda to help expand a given slice by merging a vector of slices into it
    auto expandSlice = [&](mzSlice* mergeInto, vector<mzSlice*> slices) {
        if (slices.empty())
            return;

        for (auto slice : slices) {
            mergeInto->ionCount = std::max(mergeInto->ionCount, slice->ionCount);
            mergeInto->rtmax = std::max(mergeInto->rtmax, slice->rtmax);
            mergeInto->rtmin = std::min(mergeInto->rtmin, slice->rtmin);
            mergeInto->mzmax = std::max(mergeInto->mzmax, slice->mzmax);
            mergeInto->mzmin = std::min(mergeInto->mzmin, slice->mzmin);
        }

        // calculate the new midpoints
        mergeInto->mz = (mergeInto->mzmin + mergeInto->mzmax) / 2.0f;
        mergeInto->rt = (mergeInto->rtmin + mergeInto->rtmax) / 2.0f;

        // make sure that mz window does not get out of control
        auto cutoff = massCutoff->massCutoffValue(mergeInto->mz);
        if (mergeInto->mzmin < mergeInto->mz - cutoff)
            mergeInto->mzmin =  mergeInto->mz - cutoff;
        if (mergeInto->mzmax > mergeInto->mz + cutoff)
            mergeInto->mzmax =  mergeInto->mz + cutoff;

        mergeInto->mz = (mergeInto->mzmin + mergeInto->mzmax) / 2.0f;
    };

    for(auto it = begin(slices); it != end(slices); ++it) {
        if (_mavenParameters->stop) {
            clearSlices();
            break;
        }

        sendSignal("Merging adjacent slices…",
                   it - begin(slices),
                   slices.size());

        auto slice = *it;
        vector<mzSlice*> slicesToMerge;

        // search ahead
        for (auto ahead = next(it);
             ahead != end(slices) && it != end(slices);
             ++ahead) {
            auto comparisonSlice = *ahead;
            auto comparison = _compareSlices(_samples,
                                             slice,
                                             comparisonSlice,
                                             massCutoff,
                                             rtTolerance);
            auto shouldMerge = comparison.first;
            auto continueIteration = comparison.second;
            if (shouldMerge)
                slicesToMerge.push_back(comparisonSlice);
            if (!continueIteration)
                break;
        }

        // search behind
        for (auto behind = prev(it);
             behind != begin(slices) && it != begin(slices);
             --behind) {
            auto comparisonSlice = *behind;
            auto comparison = _compareSlices(_samples,
                                             slice,
                                             comparisonSlice,
                                             massCutoff,
                                             rtTolerance);
            auto shouldMerge = comparison.first;
            auto continueIteration = comparison.second;
            if (shouldMerge)
                slicesToMerge.push_back(comparisonSlice);
            if (!continueIteration)
                break;
        }

        // expand the current slice by merging all slices classified to be
        // part of the same, and then remove (and free) the slices already
        // merged
        expandSlice(slice, slicesToMerge);
        for (auto merged : slicesToMerge) {
            slices.erase(remove_if(begin(slices),
                                   end(slices),
                                   [&](mzSlice* s) { return s == merged; }),
                         slices.end());
            delete merged;
        }
        it = find_if(begin(slices),
                     end(slices),
                     [&](mzSlice* s) { return s == slice; });
    }
}

pair<bool, bool> MassSlicer::_compareSlices(vector<mzSample*>& samples,
                                            mzSlice* slice,
                                            mzSlice* comparisonSlice,
                                            const MassCutoff *massCutoff,
                                            const float rtTolerance)
{
    auto mz = slice->mz;
    auto mzMin = slice->mzmin;
    auto mzMax = slice->mzmax;
    auto rtMin = slice->rtmin;
    auto rtMax = slice->rtmax;
    auto comparisonMz = comparisonSlice->mz;
    auto comparisonMzMin = comparisonSlice->mzmin;
    auto comparisonMzMax = comparisonSlice->mzmax;
    auto comparisonRtMin = comparisonSlice->rtmin;
    auto comparisonRtMax = comparisonSlice->rtmax;
    auto mzCenter = (mz + comparisonMz) / 2.0f;

    // check to make sure slices are close to each other (or have some
    // overlap in mz domain); the tolerance is multiplied 10x so as to
    // include slices that may be further apart but should be merged
    float massTolerance = 10.0f * massCutoff->massCutoffValue(mzCenter);
    if (!(abs(mzCenter - mz) <= massTolerance
          && abs(mzCenter - comparisonMz) <= massTolerance)) {
        return make_pair(false, false);
    }

    // check if common RT regions exist between the slices being compared
    auto commonLowerRt = 0.0f;
    auto commonUpperRt = 0.0f;
    if (rtMin <= comparisonRtMin && rtMax >= comparisonRtMax) {
        commonLowerRt = comparisonRtMin;
        commonUpperRt = comparisonRtMax;
    } else if (rtMin >= comparisonRtMin && rtMax <= comparisonRtMax) {
        commonLowerRt = rtMin;
        commonUpperRt  = rtMax;
    } else if (rtMin >= comparisonRtMin && rtMin <= comparisonRtMax) {
        commonLowerRt = rtMin;
        commonUpperRt = min(rtMax, comparisonRtMax);
    } else if (rtMax >= comparisonRtMin && rtMax <= comparisonRtMax) {
        commonLowerRt = max(rtMin, comparisonRtMin);
        commonUpperRt = rtMax;
    }
    if (commonLowerRt == 0.0f && commonUpperRt == 0.0f)
        return make_pair(false, true);

    auto highestIntensity = 0.0f;
    auto mzAtHighestIntensity = 0.0f;
    auto rtAtHighestIntensity = 0.0f;
    auto highestCompIntensity = 0.0f;
    auto mzAtHighestCompIntensity = 0.0f;
    auto rtAtHighestCompIntensity = 0.0f;
#pragma omp parallel
    {
        vector<vector<float>> eicValues;
        vector<vector<float>> comparisonEicValues;
#pragma omp for nowait
        for (size_t i = 0; i < samples.size(); ++i) {
            auto sample = samples.at(i);

            // obtain EICs for the two slices
            auto eic = sample->getEIC(mzMin,
                                      mzMax,
                                      rtMin,
                                      rtMax,
                                      1,
                                      1,
                                      "");
            auto comparisonEic = sample->getEIC(comparisonMzMin,
                                                comparisonMzMax,
                                                comparisonRtMin,
                                                comparisonRtMax,
                                                1,
                                                1,
                                                "");
            eicValues.push_back({eic->maxIntensity,
                                 eic->rtAtMaxIntensity,
                                 eic->mzAtMaxIntensity});
            comparisonEicValues.push_back({comparisonEic->maxIntensity,
                                           comparisonEic->rtAtMaxIntensity,
                                           comparisonEic->mzAtMaxIntensity});
            delete eic;
            delete comparisonEic;
        }
#pragma omp critical
        // obtain the highest intensity's mz and rt
        // these updates should happen in a single thread
        for (auto values : eicValues) {
            if (highestIntensity < values[0]) {
                highestIntensity = values[0];
                rtAtHighestIntensity = values[1];
                mzAtHighestIntensity = values[2];
            }
        }
        for (auto values : comparisonEicValues) {
            if (highestCompIntensity < values[0]) {
                highestCompIntensity = values[0];
                rtAtHighestCompIntensity = values[1];
                mzAtHighestCompIntensity = values[2];
            }
        }
    }

    if (highestIntensity == 0.0f && highestCompIntensity == 0.0f)
        return make_pair(false, true);

    // calculate and check for rt difference and mz difference, if
    // conditions are satisfied, mark the comparison slice to be merged
    auto rtDelta = abs(rtAtHighestIntensity - rtAtHighestCompIntensity);
    auto mzCenterForIntensity = (mzAtHighestIntensity
                              + mzAtHighestCompIntensity) / 2.0f;
    auto massToleranceForIntensity =
        massCutoff->massCutoffValue(mzCenterForIntensity);
    auto mzDeltaNeg = abs(mzCenterForIntensity - mzAtHighestIntensity );
    auto mzDeltaPos = abs(mzAtHighestCompIntensity - mzCenterForIntensity );
    if (rtDelta <= rtTolerance
        && mzDeltaNeg <= massToleranceForIntensity
        && mzDeltaPos <= massToleranceForIntensity) {
        return make_pair(true, true);
    }

    return make_pair(false, true);
}

void MassSlicer::_adjustSlices(MassCutoff* massCutoff)
{
    size_t progressCount = 0;
    for (auto slice : slices) {
        if (_mavenParameters->stop) {
            clearSlices();
            break;
        }

        auto eics = PeakDetector::pullEICs(slice,
                                           _mavenParameters->samples,
                                           _mavenParameters);
        float highestIntensity = 0.0f;
        float mzAtHighestIntensity = 0.0f;
        for (auto eic : eics) {
            size_t size = eic->intensity.size();
            for (int i = 0; i < size; ++i) {
                if (eic->spline[i] > highestIntensity) {
                    highestIntensity = eic->spline[i];
                    mzAtHighestIntensity = eic->mz[i];
                }
            }
        }
        float cutoff = massCutoff->massCutoffValue(mzAtHighestIntensity);
        slice->mzmin =  mzAtHighestIntensity - cutoff;
        slice->mzmax =  mzAtHighestIntensity + cutoff;
        slice->mz = (slice->mzmin + slice->mzmax) / 2.0f;

        delete_all(eics);

        ++progressCount;
        sendSignal("Adjusting slices…", progressCount, slices.size());
    }
}
