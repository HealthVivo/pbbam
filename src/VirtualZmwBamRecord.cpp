// File Description
/// \file VirtualZmwBamRecord.cpp
/// \brief Implements the VirtualZmwBamRecord class.
//
// Author: Armin Töpfer

#include "PbbamInternalConfig.h"

#include "pbbam/virtual/VirtualZmwBamRecord.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "pbbam/MoveAppend.h"
#include "pbbam/virtual/VirtualRegionType.h"
#include "pbbam/virtual/VirtualRegionTypeMap.h"

namespace PacBio {
namespace BAM {

VirtualZmwBamRecord::VirtualZmwBamRecord(std::vector<BamRecord> unorderedSources,
                                         const BamHeader& header)
    : BamRecord{header}, sources_{std::move(unorderedSources)}
{
    // Sort sources by queryStart
    std::sort(sources_.begin(), sources_.end(), [](const BamRecord& l1, const BamRecord& l2) {
        return l1.QueryStart() < l2.QueryStart();
    });
    StitchSources();
}

bool VirtualZmwBamRecord::HasVirtualRegionType(const VirtualRegionType regionType) const
{
    return virtualRegionsMap_.find(regionType) != virtualRegionsMap_.end();
}

Frames VirtualZmwBamRecord::IPDV1Frames(Orientation orientation) const
{
    const auto rawFrames = this->IPDRaw(orientation);
    const std::vector<uint8_t> rawData(rawFrames.Data().begin(), rawFrames.Data().end());
    return Frames::Decode(rawData);
}

void VirtualZmwBamRecord::StitchSources()
{
    const auto& firstRecord = sources_[0];
    const auto& lastRecord = sources_[sources_.size() - 1];

    std::string sequence;
    std::string deletionTag;
    std::string substitutionTag;
    std::string alternativeLabelTag;
    std::string pulseCall;

    QualityValues qualities;
    QualityValues deletionQv;
    QualityValues insertionQv;
    QualityValues mergeQv;
    QualityValues pulseMergeQv;
    QualityValues substitutionQv;
    QualityValues labelQv;
    QualityValues alternativeLabelQv;

    Frames ipd;
    Frames pw;
    Frames pd;
    Frames px;
    std::vector<float> pa;
    std::vector<float> pm;
    std::vector<uint32_t> sf;
    std::vector<PacBio::BAM::PulseExclusionReason> pe;

    // initialize capacity
    const auto stitchedSize = lastRecord.QueryEnd() - firstRecord.QueryStart();
    sequence.reserve(stitchedSize);
    deletionTag.reserve(stitchedSize);
    substitutionTag.reserve(stitchedSize);
    alternativeLabelTag.reserve(stitchedSize);
    pulseCall.reserve(stitchedSize);
    qualities.reserve(stitchedSize);
    deletionQv.reserve(stitchedSize);
    insertionQv.reserve(stitchedSize);
    mergeQv.reserve(stitchedSize);
    pulseMergeQv.reserve(stitchedSize);
    substitutionQv.reserve(stitchedSize);
    labelQv.reserve(stitchedSize);
    alternativeLabelQv.reserve(stitchedSize);
    ipd.DataRaw().reserve(stitchedSize);
    pw.DataRaw().reserve(stitchedSize);
    pd.DataRaw().reserve(stitchedSize);
    px.DataRaw().reserve(stitchedSize);
    pa.reserve(stitchedSize);
    pm.reserve(stitchedSize);
    sf.reserve(stitchedSize);
    pe.reserve(stitchedSize);

    // Stitch using tmp vars
    for (auto& b : sources_) {
        sequence.append(b.Sequence());

        MoveAppend(b.Qualities(), qualities);

        if (b.HasDeletionQV()) MoveAppend(std::move(b.DeletionQV()), deletionQv);

        if (b.HasInsertionQV()) MoveAppend(std::move(b.InsertionQV()), insertionQv);

        if (b.HasMergeQV()) MoveAppend(std::move(b.MergeQV()), mergeQv);

        if (b.HasPulseMergeQV()) MoveAppend(std::move(b.PulseMergeQV()), pulseMergeQv);

        if (b.HasSubstitutionQV()) MoveAppend(std::move(b.SubstitutionQV()), substitutionQv);

        if (b.HasLabelQV()) MoveAppend(std::move(b.LabelQV()), labelQv);

        if (b.HasAltLabelQV()) MoveAppend(std::move(b.AltLabelQV()), alternativeLabelQv);

        if (b.HasDeletionTag()) deletionTag.append(std::move(b.DeletionTag()));

        if (b.HasSubstitutionTag()) substitutionTag.append(std::move(b.SubstitutionTag()));

        if (b.HasAltLabelTag()) alternativeLabelTag.append(std::move(b.AltLabelTag()));

        if (b.HasPulseCall()) pulseCall.append(std::move(b.PulseCall()));

        if (b.HasIPD()) MoveAppend(b.IPDRaw().DataRaw(), ipd.DataRaw());

        if (b.HasPulseWidth()) MoveAppend(b.PulseWidthRaw().DataRaw(), pw.DataRaw());

        if (b.HasPulseCallWidth()) MoveAppend(b.PulseCallWidth().DataRaw(), px.DataRaw());

        if (b.HasPrePulseFrames()) MoveAppend(b.PrePulseFrames().DataRaw(), pd.DataRaw());

        if (b.HasPkmid()) MoveAppend(b.Pkmid(), pm);

        if (b.HasPkmean()) MoveAppend(b.Pkmean(), pa);

        if (b.HasPkmid2()) MoveAppend(b.Pkmid2(), pm);

        if (b.HasPkmean2()) MoveAppend(b.Pkmean2(), pa);

        if (b.HasPulseExclusion()) MoveAppend(b.PulseExclusionReason(), pe);

        if (b.HasStartFrame()) MoveAppend(b.StartFrame(), sf);

        if (b.HasScrapRegionType()) {
            const VirtualRegionType regionType = b.ScrapRegionType();

            if (!HasVirtualRegionType(regionType))
                virtualRegionsMap_[regionType] = std::vector<VirtualRegion>{};

            virtualRegionsMap_[regionType].emplace_back(regionType, b.QueryStart(), b.QueryEnd());
        }

        if (b.HasLocalContextFlags()) {
            std::pair<int, int> barcodes{-1, -1};
            if (b.HasBarcodes()) barcodes = b.Barcodes();

            static constexpr const auto regionType = VirtualRegionType::SUBREAD;
            if (!HasVirtualRegionType(regionType))
                virtualRegionsMap_[regionType] = std::vector<VirtualRegion>{};

            virtualRegionsMap_[regionType].emplace_back(regionType, b.QueryStart(), b.QueryEnd(),
                                                        b.LocalContextFlags(), barcodes.first,
                                                        barcodes.second);
        }

        if (b.HasBarcodes() && !this->HasBarcodes()) this->Barcodes(b.Barcodes());

        if (b.HasBarcodeQuality() && !this->HasBarcodeQuality())
            this->BarcodeQuality(b.BarcodeQuality());

        if (b.HasReadAccuracy() && !this->HasReadAccuracy()) this->ReadAccuracy(b.ReadAccuracy());

        if (b.HasScrapZmwType()) {
            if (!this->HasScrapZmwType())
                this->ScrapZmwType(b.ScrapZmwType());
            else if (this->ScrapZmwType() != b.ScrapZmwType())
                throw std::runtime_error{"ScrapZmwTypes do not match"};
        }
    }

    // ReadGroup
    this->ReadGroup(this->header_.ReadGroups()[0]);

    this->NumPasses(1);

    // All records should contain the same SNR and hole number
    if (firstRecord.HasSignalToNoise()) this->SignalToNoise(firstRecord.SignalToNoise());
    this->HoleNumber(firstRecord.HoleNumber());

    // QueryStart
    this->QueryStart(firstRecord.QueryStart());
    this->QueryEnd(lastRecord.QueryEnd());
    this->UpdateName();

    const std::string qualitiesStr = qualities.Fastq();
    if (sequence.size() == qualitiesStr.size())
        this->Impl().SetSequenceAndQualities(sequence, qualitiesStr);
    else
        this->Impl().SetSequenceAndQualities(sequence);

    // Tags as strings
    if (!deletionTag.empty()) this->DeletionTag(deletionTag);
    if (!substitutionTag.empty()) this->SubstitutionTag(substitutionTag);
    if (!alternativeLabelTag.empty()) this->AltLabelTag(alternativeLabelTag);
    if (!pulseCall.empty()) this->PulseCall(pulseCall);

    // QVs
    if (!deletionQv.empty()) this->DeletionQV(deletionQv);
    if (!insertionQv.empty()) this->InsertionQV(insertionQv);
    if (!mergeQv.empty()) this->MergeQV(mergeQv);
    if (!pulseMergeQv.empty()) this->PulseMergeQV(pulseMergeQv);
    if (!substitutionQv.empty()) this->SubstitutionQV(substitutionQv);
    if (!labelQv.empty()) this->LabelQV(labelQv);
    if (!alternativeLabelQv.empty()) this->AltLabelQV(alternativeLabelQv);

    // PulseExclusionReason
    if (!pe.empty()) this->PulseExclusionReason(pe);

    // 16 bit arrays
    if (!ipd.Data().empty()) this->IPD(ipd, FrameEncodingType::LOSSLESS);
    if (!pw.Data().empty()) this->PulseWidth(pw, FrameEncodingType::LOSSLESS);
    if (!pa.empty()) this->Pkmean(pa);
    if (!pm.empty()) this->Pkmid(pm);
    if (!pd.Data().empty()) this->PrePulseFrames(pd, FrameEncodingType::LOSSLESS);
    if (!px.Data().empty()) this->PulseCallWidth(px, FrameEncodingType::LOSSLESS);

    // 32 bit arrays
    if (!sf.empty()) this->StartFrame(sf);

    // Determine HQREGION bases on LQREGIONS
    if (HasVirtualRegionType(VirtualRegionType::LQREGION)) {
        if (virtualRegionsMap_[VirtualRegionType::LQREGION].size() == 1) {
            const auto lq = virtualRegionsMap_[VirtualRegionType::LQREGION][0];
            if (lq.beginPos == 0)
                virtualRegionsMap_[VirtualRegionType::HQREGION].emplace_back(
                    VirtualRegionType::HQREGION, lq.endPos, sequence.size());
            else if (lq.endPos == static_cast<int>(sequence.size()))
                virtualRegionsMap_[VirtualRegionType::HQREGION].emplace_back(
                    VirtualRegionType::HQREGION, 0, lq.beginPos);
            else
                throw std::runtime_error{"Unknown HQREGION"};
        } else {
            int beginPos = 0;
            for (const auto& lqregion : virtualRegionsMap_[VirtualRegionType::LQREGION]) {
                if (lqregion.beginPos - beginPos > 0)
                    virtualRegionsMap_[VirtualRegionType::HQREGION].emplace_back(
                        VirtualRegionType::HQREGION, beginPos, lqregion.beginPos);
                beginPos = lqregion.endPos;
            }
        }
    } else {
        virtualRegionsMap_[VirtualRegionType::HQREGION].emplace_back(VirtualRegionType::HQREGION, 0,
                                                                     sequence.size());
    }
}

std::map<VirtualRegionType, std::vector<VirtualRegion>> VirtualZmwBamRecord::VirtualRegionsMap()
    const
{
    return virtualRegionsMap_;
}

std::vector<VirtualRegion> VirtualZmwBamRecord::VirtualRegionsTable(
    const VirtualRegionType regionType) const
{
    const auto iter = virtualRegionsMap_.find(regionType);
    if (iter != virtualRegionsMap_.cend()) return iter->second;
    return {};
}

}  // namespace BAM
}  // namespace PacBio
