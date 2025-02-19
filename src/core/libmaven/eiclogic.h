#ifndef EICLOGIC_H
#define EICLOGIC_H

#include "datastructures/mzSlice.h"
#include "PeakGroup.h"
#include "standardincludes.h"

class Compound;
class EIC;
class mzSlice;
class MassCutoff;
class mzSample;
class MavenParameters;

class EICLogic {
public:
	EICLogic();

	vector<EIC*> getEICs() {
		return eics;
	}
	vector<PeakGroup>& getPeakGroups() {
		return peakgroups;
	}

    /**
     * @brief Get the last selected group for EIC.
     * @details This group can have different origins depending on which UI
     * entity last interacted with the associated EIC widget. It can be used to
     * access the original peak group, which is still selected in whatever
     * widget it came from, while the visualization might have been modified to
     * show different slices related to this group.
     * @return Shared pointer to a `PeakGroup` object.
     */
    inline shared_ptr<PeakGroup> selectedGroup() { return _selectedGroup; }

    /**
     * @brief Get the currently displayed peak group in EIC.
     * @details This will point to nothing, in case the EIC is only showing a
     * slice and not an actual materialized group. Use `selectedGroup` instead
     * to access a group that was originally shown but has now been modified.
     * @return shared pointer to a `PeakGroup` object.
     */
    inline shared_ptr<PeakGroup> displayedGroup() { return _displayedGroup; }

    /**
     * @brief Set the currently selected group in whichever widget that last
     * commanded EIC widget to diplay a peak group.
     * @param group Shared pointer to a `PeakGroup` object.
     */
    inline void setSelectedGroup(shared_ptr<PeakGroup> group)
    {
        _selectedGroup = group;
    }

    /**
     * @brief Set the peak group currently visible in the associated EIC widget.
     * @param group Shared pointer to a `PeakGroup` object.
     */
    inline void setDisplayedGroup(shared_ptr<PeakGroup> group)
    {
        _displayedGroup = group;
    }

	mzSlice& getMzSlice() {
		return _slice;
	}

	PeakGroup* selectGroupNearRt(float rt,
								PeakGroup* selGroup,
								bool matchRtFlag,
								int qualityWeight,
								int intensityWeight,
								int deltaRTWeight);

    void groupPeaks(mzSlice* slice, MavenParameters* mp);

	mzSlice setMzSlice(float mz1, MassCutoff *massCutoff, float mz2 = 0.0);

	//	find absolute min and max for extracted ion chromatograms
	mzSlice visibleEICBounds();

	//	find absolute min and max for samples
	mzSlice visibleSamplesBounds(vector<mzSample*> samples);

    void getEIC(mzSlice bounds, vector<mzSample*> samples, MavenParameters* mp);

        //associate compound names with peak groups
	void associateNameWithPeakGroups();

	mzSlice _slice;						// current slice
	vector<EIC*> eics;				// vectors mass slices one from each sample
	deque<EIC*> tics;				// vectors total chromatogram intensities
	vector<PeakGroup> peakgroups;	    //peaks grouped across samples

private:
	void addPeakGroup(PeakGroup& group);
    shared_ptr<PeakGroup> _selectedGroup;
    shared_ptr<PeakGroup> _displayedGroup;
};

#endif // EICLOGIC_H
