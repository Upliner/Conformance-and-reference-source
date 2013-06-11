#include <limits>
#include "HelperMethods.h"
#include "PostprocessData.h"

#define scaleToTIR(x) ((long double)(x)/(long double)tir->mediaTimeScale)

void checkDASHBoxOrder(long cnt, atomOffsetEntry *list, long segmentInfoSize, bool initializationSegment, UInt64 *segmentSizes, MovieInfoRec *mir)
{
    UInt64 offset = 0;

    bool psshInInit = false;
    bool tencInInit = false;
    
    if(initializationSegment)
    {
        for (int i = 0; i < cnt; i++) 
        {
            if(list[i].offset < segmentSizes[0])
            {
                if (list[i].type == 'moof')
                {
                    errprint("moof found in initialization segment: Section 6.3.3. of ISO/IEC 23009-1:2012(E): It shall not contain any \"moof\" boxes\n");
                }
                else if (list[i].type == 'mdat')
                {
                    errprint("mdat found in initialization segment: Section 6.3.4.2. of ISO/IEC 23009-1:2012(E): The Initialization Segment shall not contain any media data with an assigned presentation time.\n");
                }

                if(vg.dash264enc && list[i].type == 'pssh')
                    psshInInit = true;
                
                if(vg.dash264enc && list[i].type == 'tenc')
                    tencInInit = true;
            }
        }
        
        offset+=segmentSizes[0];
    }
    
    bool sidxFoundInPreviousSegment = false;
    bool sidxFound = false;
    bool ftypFound = false;
        
    for(int index = (initializationSegment ? 1 : 0) ; index < segmentInfoSize ; index++)
    {
        bool boxAtSegmentStartFound = false;
        bool sidxFoundInSegment = false;
        bool ssixFoundInSegment = false;
        bool psshFoundInSegment = false;
        bool tencFoundInSegment = false;
        
        for (int i = 0; i < cnt; i++) 
        {
            if(list[i].offset >= (offset+segmentSizes[index]))   //Segment end
            {
                break;
            }
            
            if (list[i].offset == offset)
            {
                boxAtSegmentStartFound = true;
               
                if (list[i].type != 'styp' && list[i].type != 'sidx' && list[i].type != 'moof' && (list[i].type != 'ftyp' || initializationSegment))
                {
                    if(list[i].type == 'ftyp' && initializationSegment)
                        warnprint("ftyp box found in the begining of a media segment while initializatioin segment is provided!\n");
                    else
                        warnprint("Unexpected box type %s found at the begining of segment %d.\n",ostypetostr(list[i].type),index+1);
                }

                bool fragmentInSegmentFound = false;
                bool moovInSegmentFound = false;

                for(int j = i ; list[j].offset < (offset+segmentSizes[index]) ; j++)
                {
					if(list[j].type == 'ftyp') {
						ftypFound = 1;
					}
					else if(list[j].type == 'moov') {

                        moovInSegmentFound = true;
                        
						if (ftypFound) {
							initializationSegment = 1;
						} else if(index == 0){
							errprint("no ftyp box found, violating: Section 4.3 of ISO/IEC 14496-12:2012(E)\n");
						}
					}

                    if(vg.dash264enc && list[i].type == 'pssh')
                        psshFoundInSegment = true;
                    
                    if(vg.dash264enc && list[i].type == 'tenc')
                        tencFoundInSegment = true;
                    
                    if(list[j].type == 'moof')
                    {                        
                        if (j == (cnt-1) || list[j+1].offset >= (offset+segmentSizes[index]) || list[j+1].type != 'mdat')
                            errprint("mdat not found following a moof in segment %d (at file absolute offset %lld), violating: Section 6.3.4.2. of ISO/IEC 23009-1:2012(E): Each Media Segment shall contain one or more whole self-contained movie fragments. A whole, self-contained movie fragment is a movie fragment ('moof') box and a media data ('mdat') box that contains all the media samples that do not use external data references referenced by the track runs in the movie fragment box.\n",index,list[j].offset);

                        fragmentInSegmentFound = true;
                    }
                    
                    if(list[j].type == 'sidx' && vg.simsInStyp[index] && !ssixFoundInSegment)
                    {                        
                        if (j == (cnt-1) || list[j+1].offset >= (offset+segmentSizes[index]) || list[j+1].type != 'ssix')
                            errprint("ssix not found following the sidx in segment %d (at file absolute offset %lld), violating: Section 6.3.4.4. of ISO/IEC 23009-1:2012(E): The Subsegment Index box ('ssix') shall be present and shall follow immediately after the 'sidx' box that documents the same Subsegment\n",index,list[j].offset);

                        ssixFoundInSegment = true;
                    }
                    /*JLF: this is only valid for onDemand profile (8.3.3) and live (8.4.3)*/
					if (fragmentInSegmentFound && (list[j].type == 'sidx' || list[j].type == 'ssix')) {
						if (vg.isoondemand || vg.dash264base)
						{
		                        errprint("Indexing information (sidx/ssix) found in segment %d (at file absolute offset %lld) following a moof, violating: ",index,list[j].offset);

                                if(vg.isoondemand)
                                    errprint("Section 8.3.3. of ISO/IEC 23009-1:2012(E): All Segment Index ('sidx') and Subsegment Index ('ssix') boxes shall be placed before any Movie Fragment ('moof') boxes\n");
                                else
                                    errprint("Section 3.2.3. Interoperability Point DASH264: In Media Segments, all Segment Index ('sidx') and Subsegment Index ('ssix') boxes, if present, shall be placed before any Movie Fragment ('moof') boxes.\n");
						}
						else if (vg.isoLive)
		                        errprint("Indexing information (sidx/ssix) found in segment %d (at file absolute offset %lld) following a moof, violating: Section 8.4.3. of ISO/IEC 23009-1:2012(E): In Media Segments, all Segment Index ('sidx') and Subsegment Index ('ssix') boxes shall be placed before any Movie Fragment ('moof') boxes\n",index,list[j].offset);
					}
                }

                if(!fragmentInSegmentFound && !initializationSegment)
                    errprint("No fragment found in segment %d\n",index+1);

                if(vg.dsms[index] && !moovInSegmentFound)
                    errprint("Segment %d has dsms compatible brand (Self-initializing media segment), however, moov box not found in this segment as expected.\n",index+1);
                
                if(vg.dash264enc && !psshInInit && !psshFoundInSegment)
                    errprint("DASH264 DRM checks: No pssh found in initialization segment and also missing in media Segment %d.\n",index+1);

                if(vg.dash264enc && !tencInInit && !tencFoundInSegment)
                    errprint("DASH264 DRM checks: No tenc found in initialization segment and also missing in media Segment %d.\n",index+1);
            }

            if(boxAtSegmentStartFound == true)
            {
                if(list[i].type == 'sidx')
                {
                    sidxFoundInSegment = true;
                    sidxFound = true;

                    if(!initializationSegment && !vg.msixInFtyp)
                        errprint("msix not found in ftyp of a self-intializing segment %d, indxing info found, violating: Section 6.3.4.3. of ISO/IEC 23009-1:2012(E): Each Media Segment shall carry 'msix' as a compatible brand \n",index);
                        
                }
                
                if(list[i].type == 'ssix')
                {
                    ssixFoundInSegment = true;
                    if(!vg.simsInStyp[index])
                        errprint("ssix found in Segment %d, but brand 'sims' not found in the styp for the segment, violating: Section 6.3.4.4. of ISO/IEC 23009-1:2012(E): It shall carry 'sims' in the Segment Type box ('styp') as a compatible brand.",index);
                }
            }

        }

        if(!boxAtSegmentStartFound)
            errprint("No box start found at the segment boundary for segment %d\n",index+1);

        if(index > (initializationSegment ? 1 : 0) && (sidxFoundInSegment != sidxFoundInPreviousSegment)) //Change of coding after first segment
        {
            if(sidxFoundInSegment)
                errprint("sidx found in Segment number %d, while it was missing in an the previous segment, violating: Section 6.3.4.3. of ISO/IEC 23009-1:2012(E): Each Media Segment shall contain one or more 'sidx' boxes. \n",index+1);
            else
                errprint("sidx not found in Segment number %d, while it has been found at least in the previous segment, violating: Section 6.3.4.3. of ISO/IEC 23009-1:2012(E): Each Media Segment shall contain one or more 'sidx' boxes. \n",index+1);
        }

        sidxFoundInPreviousSegment = sidxFoundInSegment;

        offset+=segmentSizes[index];
    }

	if(vg.msixInFtyp && !sidxFound)
        errprint("No indexing info found while 'msix' was a compatible brand: Section 6.3.4.3. of ISO/IEC 23009-1:2012(E): Each Media Segment shall carry 'msix' as a compatible brand \n");

}

OSErr postprocessFragmentInfo(MovieInfoRec *mir)
{
    UInt32 i;

    for(i = 0 ; i < (UInt32)mir->numTIRs ; i++)
    {
     mir->tirList[i].cumulatedTackFragmentDecodeTime = 0;
    }
    
    for(i = 0 ; i < mir->numFragments ; i++)
    {
        
        for(long k = 0 ; k < mir->numTIRs ; k++)
        {
            mir->moofInfo[i].tfdt[k] = mir->tirList[k].cumulatedTackFragmentDecodeTime;
        }
        
        if(mir->moofInfo[i].numTrackFragments > 0)
        {
                UInt32 j;
                    
                for(j = 0 ; j < mir->moofInfo[i].numTrackFragments ; j++)
                {
                    UInt32 index = getTrakIndexByID(mir->moofInfo[i].trafInfo[j].track_ID);

                    if(index >= (UInt32)mir->numTIRs)
                        return badAtomErr;
                    
                    if(mir->moofInfo[i].trafInfo[j].tfdtFound)
                    {
                        if(mir->moofInfo[i].trafInfo[j].baseMediaDecodeTime != mir->tirList[index].cumulatedTackFragmentDecodeTime)
                        {
                            if(i == 0 && vg.dashSegment)
                            {
                                warnprint("tfdt base media decode time %Lf not equal to accumulated decode time %Lf for track %d for the first fragment of the movie. This software does not handle incomplete presentations. Applying correction.\n",(long double)mir->moofInfo[i].trafInfo[j].baseMediaDecodeTime/(long double)mir->tirList[index].mediaTimeScale,(long double)mir->tirList[index].cumulatedTackFragmentDecodeTime/(long double)mir->tirList[index].mediaTimeScale,mir->moofInfo[i].trafInfo[j].track_ID);                                
                                mir->tirList[index].cumulatedTackFragmentDecodeTime = mir->moofInfo[i].trafInfo[j].baseMediaDecodeTime;
                            }
                            else
                                errprint("tfdt base media decode time %Lf not equal to accumulated decode time %Lf for track %d for sequence_number %d (fragment absolute count %d)\n",(long double)mir->moofInfo[i].trafInfo[j].baseMediaDecodeTime/(long double)mir->tirList[index].mediaTimeScale,(long double)mir->tirList[index].cumulatedTackFragmentDecodeTime/(long double)mir->tirList[index].mediaTimeScale,mir->moofInfo[i].trafInfo[j].track_ID,mir->moofInfo[i].sequence_number,i+1);
                        }

                    }
                    mir->tirList[index].cumulatedTackFragmentDecodeTime += mir->moofInfo[i].trafInfo[j].cummulatedSampleDuration;
                }
        }
    }
	return noErr;
}

void initializeLeafInfo(MovieInfoRec *mir, long numMediaSegments)
{
    
    for(long i = 0 ; i < mir->numTIRs ; i++)
    {
        if(mir->tirList[i].numLeafs > 0)    //Indexed
        {
            mir->tirList[i].leafInfo = (LeafInfo *)malloc(mir->tirList[i].numLeafs * sizeof(LeafInfo));

            for(UInt32 j = 0 ; j < mir->tirList[i].numLeafs ; j++)
                mir->tirList[i].leafInfo[j].segmentIndexed = true;

            //Rest of the intialization of presentation times will be done later in processIndexingInfo, that is a better place since there we process indexing information properly.
        }
        else
        {
            mir->tirList[i].numLeafs = numMediaSegments;
            
            mir->tirList[i].leafInfo = (LeafInfo *)malloc(mir->tirList[i].numLeafs * sizeof(LeafInfo));

            for(UInt32 j = 0 ; j < mir->tirList[i].numLeafs ; j++)
                mir->tirList[i].leafInfo[j].segmentIndexed = false;

            UInt32 mediaSegmentNumber = 0;
            
            for(UInt32 k = 0 ; k < mir->numFragments ; k++)
            {
                if(k > 0 ? checkSegmentBoundry(mir->moofInfo[k - 1].offset, mir->moofInfo[k].offset) : true)
                {
                    MoofInfoRec *moof = &mir->moofInfo[k];

                    moof->firstFragmentInSegment = true;
                    mir->tirList[i].leafInfo[mediaSegmentNumber].firstMoofIndex = moof->index;
                    mir->tirList[i].leafInfo[mediaSegmentNumber].firstInSegment = true;
                    mir->tirList[i].leafInfo[mediaSegmentNumber].earliestPresentationTime = moof->moofEarliestPresentationTimePerTrack[i];
                    mir->tirList[i].leafInfo[mediaSegmentNumber].sidxReportedDuration = 0;  //Not indexed
                    mir->tirList[i].leafInfo[mediaSegmentNumber].hasFragments = true;

                    if(mediaSegmentNumber > 0)
                    {
                        mir->tirList[i].leafInfo[mediaSegmentNumber - 1].lastMoofIndex = mir->moofInfo[k-1].index;
                        mir->tirList[i].leafInfo[mediaSegmentNumber - 1].lastPresentationTime = mir->moofInfo[k-1].moofLastPresentationTimePerTrack[i];
                        mir->tirList[i].leafInfo[mediaSegmentNumber - 1].presentationEndTime = mir->moofInfo[k-1].moofPresentationEndTimePerTrack[i];
                    }

                    if((long)mediaSegmentNumber == (numMediaSegments - 1))
                    {
                        mir->tirList[i].leafInfo[mediaSegmentNumber].lastMoofIndex = mir->moofInfo[mir->numFragments-1].index;
                        mir->tirList[i].leafInfo[mediaSegmentNumber].lastPresentationTime = mir->moofInfo[mir->numFragments-1].moofLastPresentationTimePerTrack[i];
                        mir->tirList[i].leafInfo[mediaSegmentNumber].presentationEndTime = mir->moofInfo[mir->numFragments-1].moofPresentationEndTimePerTrack[i];
                    }

                    mediaSegmentNumber++;
                }
            }
        }
    }
}

void estimatePresentationTimes(MovieInfoRec *mir)
{
    //Calcualte deltas based on edit lists
    for(int  i = 0 ; i < mir->numTIRs ; i++)
    {
        TrackInfoRec *tir = &(mir->tirList[i]);

        SInt64 presentationTime = 0;

        for(UInt32 e = 0 ; e < tir->numEdits ; e++)
        {
            if(tir->elstInfo[e].mediaTime < 0)
            {
                printf("Empty edits not handled. Processing unreliable.\n");
            }
            
            for(UInt32 j = 0 ; j < mir->numFragments; j++)
            {
                UInt64 cummulatedDuration = 0;
                MoofInfoRec *moof = &mir->moofInfo[j];
                moof->samplesToBePresented = false;
                
                for(UInt32 k = 0; k < moof->numTrackFragments ; k++)
                {
                     if(moof->trafInfo[k].track_ID == tir->trackID && moof->trafInfo[k].numTrun > 0)//Assuming 'trun' cannot be empty, 14496-12 version 4 does not indicate such a possiblity.
                     {
                         for(UInt32 l = 0 ; l < moof->trafInfo[k].numTrun ; l++)
                         {
                             for(UInt32 m = 0 ; m < moof->trafInfo[k].trunInfo[l].sample_count ; m++)
                             {
                                 SInt64 sample_composition_time_offset = moof->trafInfo[k].trunInfo[l].version != 0 ? (SInt64)((Int32)moof->trafInfo[k].trunInfo[l].sample_composition_time_offset[m]) : (UInt32)moof->trafInfo[k].trunInfo[l].sample_composition_time_offset[m];
                                 SInt64 sampleCompositionTime = sample_composition_time_offset + (SInt64)moof->tfdt[i] + cummulatedDuration;

                                 if(tir->elstInfo[e].mediaTime > 0)
                                 {
                                     if(sampleCompositionTime >= tir->elstInfo[e].mediaTime && sampleCompositionTime < (tir->elstInfo[e].mediaTime + (SInt64)tir->elstInfo[e].duration))
                                     {
                                        moof->trafInfo[k].trunInfo[l].samplePresentationTime[m] = 0 - (tir->elstInfo[e].mediaTime - presentationTime);  //Save the delta in: presentationTime = CompositionTime - (editMediaTime_i - presntationDuration)
                                        moof->trafInfo[k].trunInfo[l].sampleToBePresented[m] = true;
                                        moof->samplesToBePresented = true;
                                     }
                                     else if(sampleCompositionTime >= tir->elstInfo[e].mediaTime) // A later edit should update this. Else sample not to be presented
                                        moof->trafInfo[k].trunInfo[l].sampleToBePresented[m] = false;
                                 }
                                 else
                                     moof->trafInfo[k].trunInfo[l].sampleToBePresented[m] = false;

                                 cummulatedDuration += moof->trafInfo[k].trunInfo[l].sample_duration[m];
                             }
                         }
                     }
                }
            }
            
            presentationTime+=(SInt64)tir->elstInfo[e].duration;
        }
    }
    
    //Apply deltas to samples. This is done separatley since absence of an edit list is an entirely different case.
    
    for(int  i = 0 ; i < mir->numTIRs ; i++)
    {
        TrackInfoRec *tir = &(mir->tirList[i]);

        for(UInt32 j = 0 ; j < mir->numFragments; j++)
        {
            SInt64 cummulatedDuration = 0;
            MoofInfoRec *moof = &mir->moofInfo[j];

            moof->moofPresentationEndTimePerTrack[i] = j > 0 ? mir->moofInfo[j-1].moofPresentationEndTimePerTrack[i] : 0;
            moof->moofLastPresentationTimePerTrack[i] = j > 0 ? mir->moofInfo[j-1].moofPresentationEndTimePerTrack[i] : 0;
            moof->moofEarliestPresentationTimePerTrack[i] = std::numeric_limits<long double>::max();
            
            for(UInt32 k = 0; k < moof->numTrackFragments ; k++)
            {
                 if(moof->trafInfo[k].track_ID == tir->trackID && moof->trafInfo[k].numTrun > 0)    //Assuming 'trun' cannot be empty, 14496-12 version 4 does not indicate such a possiblity.
                 {
                     for(UInt32 l = 0 ; l < moof->trafInfo[k].numTrun ; l++)
                     {
                         for(UInt32 m = 0 ; m < moof->trafInfo[k].trunInfo[l].sample_count ; m++)
                         {
                             SInt64 sample_composition_time_offset = moof->trafInfo[k].trunInfo[l].version != 0 ? (SInt64)((Int32)moof->trafInfo[k].trunInfo[l].sample_composition_time_offset[m]) : (UInt32)moof->trafInfo[k].trunInfo[l].sample_composition_time_offset[m];
                             SInt64 sampleCompositionTime = sample_composition_time_offset + (SInt64)moof->tfdt[i] + cummulatedDuration;

                             moof->trafInfo[k].trunInfo[l].samplePresentationTime[m] = (long double)(moof->trafInfo[k].trunInfo[l].samplePresentationTime[m] + sampleCompositionTime)/(long double)tir->mediaTimeScale;

                             if(moof->trafInfo[k].trunInfo[l].sampleToBePresented[m] && moof->trafInfo[k].trunInfo[l].samplePresentationTime[m] >= moof->moofLastPresentationTimePerTrack[i])
                             {
                                moof->moofLastPresentationTimePerTrack[i] = moof->trafInfo[k].trunInfo[l].samplePresentationTime[m];
                             }
                                
                             if(j == (mir->numFragments - 1) || !mir->moofInfo[j+1].samplesToBePresented) //Only for last fragment, or if next fragment doesnt have presentable samples, use sample delta to calculate durations. Otherwise it is estimated from the EPT of the next moof, done after the loop
                             {
                                 long double samplePresentationEndTime = moof->trafInfo[k].trunInfo[l].samplePresentationTime[m] + (long double)moof->trafInfo[k].trunInfo[l].sample_duration[m]/(long double)tir->mediaTimeScale;
                                 
                                 if(moof->trafInfo[k].trunInfo[l].sampleToBePresented[m] && (samplePresentationEndTime > moof->moofPresentationEndTimePerTrack[i]))
                                    moof->moofPresentationEndTimePerTrack[i] = samplePresentationEndTime;
                             }
                             
                             if(moof->trafInfo[k].trunInfo[l].sampleToBePresented[m] && (moof->trafInfo[k].trunInfo[l].samplePresentationTime[m] < moof->moofEarliestPresentationTimePerTrack[i]))
                                moof->moofEarliestPresentationTimePerTrack[i] = moof->trafInfo[k].trunInfo[l].samplePresentationTime[m];

                             cummulatedDuration += moof->trafInfo[k].trunInfo[l].sample_duration[m];
                         }
                     }
                 }
            }

            if(moof->moofEarliestPresentationTimePerTrack[i] == std::numeric_limits<long double>::max())//Still uninitialized
                moof->moofEarliestPresentationTimePerTrack[i] = j > 0 ?  mir->moofInfo[j-1].moofPresentationEndTimePerTrack[i] : 0;

            if(j > 0 && moof->samplesToBePresented)
            {
               mir->moofInfo[j-1].moofPresentationEndTimePerTrack[i] = moof->moofEarliestPresentationTimePerTrack[i];
            }
        
        }
        
    }

    initializeLeafInfo(mir,vg.segmentInfoSize - (vg.initializationSegment ? 1 : 0));
}

void processSAP34(MovieInfoRec *mir)
{
    for(long i = 0 ; i < mir->numTIRs ; i++)
    {
        TrackInfoRec *tir = &(mir->tirList[i]);

        for(UInt32 j = 0 ; j < mir->numFragments; j++)
        {
            MoofInfoRec *moof = &mir->moofInfo[j];

            for(UInt32 k = 0; k < moof->numTrackFragments ; k++)
            {
                if(moof->trafInfo[k].numSbgp == 0)
                    continue;
                
                UInt32 numSamples = 0;
                
                for(UInt32 l = 0 ; l < moof->trafInfo[k].numSbgp ; l++)
                {
                    for(UInt32 m = 0 ; m < moof->trafInfo[k].sbgpInfo[l].entry_count ; m++)
                        numSamples+=moof->trafInfo[k].sbgpInfo[l].sample_count[m];
                }

                bool *sap3 = (bool *)malloc(sizeof(bool)*numSamples);
                bool *sap4 = (bool *)malloc(sizeof(bool)*numSamples);

                UInt32 sampleIndex = 0;
                
                for(UInt32 l = 0 ; l < moof->trafInfo[k].numSbgp ; l++)
                {
                    for(UInt32 m = 0 ; m < moof->trafInfo[k].sbgpInfo[l].entry_count ; m++)
                    {
                        for(UInt32 n = 0 ; n < moof->trafInfo[k].sbgpInfo[l].sample_count[m] ; n++, sampleIndex++)
                        {
                            sap3[sampleIndex] = (moof->trafInfo[k].sbgpInfo[l].grouping_type == 'rap ');

                            if(moof->trafInfo[k].sbgpInfo[l].grouping_type == 'roll' && (tir->hdlrInfo->componentSubType == 'vide' || tir->hdlrInfo->componentSubType == 'soun'))
                            {
                                UInt32 sgpdIndex = getSgpdIndex(moof->trafInfo[k].sgpdInfo, moof->trafInfo[k].numSgpd, moof->trafInfo[k].sbgpInfo[l].grouping_type);
                                if(sgpdIndex == moof->trafInfo[k].numSgpd)
                                {
                                    errprint("grouping_type %s in sbgp is not found for any sgpd in moof number %d\n",ostypetostr(moof->trafInfo[k].sbgpInfo[l].grouping_type),j+1);
                                    continue;
                                }

                                SgpdInfoRec *sgpd = &moof->trafInfo[k].sgpdInfo[sgpdIndex];

                                AudioVisualRollRecoveryEntry *avRollRecoveryEntry;

                                avRollRecoveryEntry = (AudioVisualRollRecoveryEntry *)sgpd->SampleGroupDescriptionEntry[moof->trafInfo[k].sbgpInfo[l].group_description_index[m]];

                                if(avRollRecoveryEntry->roll_distance > 0)
                                    sap4[sampleIndex] = true;
                                else
                                    sap4[sampleIndex] = false;
                            }
                            else
                                sap4[sampleIndex] = false;
                        }
                    }
                    
                }
                    
                for(UInt32 l = 0, sampleIndex = 0; l < moof->trafInfo[k].numTrun ; l++)
                {
                    for(UInt32 m = 0 ; m < moof->trafInfo[k].trunInfo[l].sample_count; m++, sampleIndex++)
                    {
                        if(sampleIndex >= numSamples)
                        {
                            errprint("Entries in sbgp (%d) is less than the corresponding number of samples (%d) in the traf %d for moof number %d\n",numSamples,sampleIndex+1,k+1,j+1);
                            continue;
                        }
                        
                        moof->trafInfo[k].trunInfo[l].sap3[m] = sap3[sampleIndex];
                        moof->trafInfo[k].trunInfo[l].sap4[m] = sap4[sampleIndex];
                    }
                }

                if(sampleIndex != numSamples)
                    errprint("Entries in sbgp (%d) are not equal to the corresponding number of samples (%d) in the traf %d for moof number %d\n",numSamples,sampleIndex,k+1,j+1);

                free(sap3);
                free(sap4);
            }
        }
    }
}

void verifyLeafDurations(MovieInfoRec *mir)
{
    for(int i = 0 ; i < mir->numTIRs ; i++)
    {
        TrackInfoRec *tir = &(mir->tirList[i]);

        if(!tir->leafInfo[0].segmentIndexed) //Process only for indexed streams
            continue;

        for(UInt32 j = 0 ; j < tir->numLeafs ; j++)
        {
            long double diff = ABS((tir->leafInfo[j].presentationEndTime - tir->leafInfo[j].earliestPresentationTime) - tir->leafInfo[j].sidxReportedDuration);

            if(!tir->leafInfo[j].hasFragments)
                continue;
            
            if(diff > (long double)1.0/(long double)tir->mediaTimeScale)
                errprint("Referenced track duration %Lf of track %d does not match to subsegment_duration %Lf for leaf with EPT %Lf, difference %Le, threshold %Le\n",(tir->leafInfo[j].presentationEndTime - tir->leafInfo[j].earliestPresentationTime),tir->trackID,tir->leafInfo[j].sidxReportedDuration,tir->leafInfo[j].earliestPresentationTime,diff,(long double)1.0/(long double)tir->mediaTimeScale);
        }
    }
}

void checkNonIndexedSamples(MovieInfoRec *mir)
{
    UInt32 accessUnitDuration = 0;
    
    for(int i = 0 ; i < mir->numTIRs ; i++)
    {
        TrackInfoRec *tir = &(mir->tirList[i]);

        if(tir->leafInfo[0].segmentIndexed)
            continue;   //An indexed stream

        bool reportInequalDuration = true;
        bool reportInequalControlDuration = true;
        UInt64 nonSyncSamples = 0;
        UInt64 syncSamples = 0;
                
        for(UInt32 j = 0 ; j < mir->numFragments ; j++)
        {
            MoofInfoRec *moof = &mir->moofInfo[j];
            
            for(UInt32 k = 0; k < moof->numTrackFragments ; k++)
            {
                if(moof->trafInfo[k].track_ID == tir->trackID && moof->trafInfo[k].numTrun > 0) //Assuming 'trun' cannot be empty, 14496-12 version 4 does not indicate such a possiblity.
                {
                    for(UInt32 l = 0 ; l < moof->trafInfo[k].numTrun ; l++)
                    {
                        for(UInt32 m = 0 ; m < moof->trafInfo[k].trunInfo[l].sample_count ; m++)
                        {            
                            bool sample_is_non_sync_sample = (moof->trafInfo[k].trunInfo[l].sample_flags[m] & 0x10000 >> 16) != 0;
                            bool sample_is_SAP = !sample_is_non_sync_sample || moof->trafInfo[k].trunInfo[l].sap3[m] || moof->trafInfo[k].trunInfo[l].sap4[m];
                            
                            if(sample_is_SAP)
                                syncSamples++;
                            else
                                nonSyncSamples++;

                            if(accessUnitDuration == 0)
                                accessUnitDuration = moof->trafInfo[k].trunInfo[l].sample_duration[m];

                            if(reportInequalDuration && moof->trafInfo[k].trunInfo[l].sample_duration[m] != accessUnitDuration)
                            {
                                errprint("Sample duration (%lu) of at least one sample of a non-index track (%d) is inequal to a previously reported duration (%lu), violating Section 7.2.2. of ISO/IEC 23009-1:2012(E): non-indexed media streams in all Representations of an Adaptation Set shall have the same access unit duration\n",moof->trafInfo[k].trunInfo[l].sample_duration[m],mir->tirList[i].trackID,accessUnitDuration);
                                reportInequalDuration = false;
                            }
                            
                            if(vg.accessUnitDurationNonIndexedTrack != 0 && reportInequalControlDuration && moof->trafInfo[k].trunInfo[l].sample_duration[m] != vg.accessUnitDurationNonIndexedTrack)
                            {
                                errprint("Control sample duration %lu is inequal to the sample duration of this stream (%lu) for non-indexed track (%d), violating Section 7.2.2. of ISO/IEC 23009-1:2012(E): non-indexed media streams in all Representations of an Adaptation Set shall have the same access unit duration\n",vg.accessUnitDurationNonIndexedTrack,moof->trafInfo[k].trunInfo[l].sample_duration[m],mir->tirList[i].trackID);
                                reportInequalControlDuration = false;
                            }
                        }
                    }
                }
            }
        }

        if(nonSyncSamples > 0)
            errprint("%lld non-sync samples found out of total %lld samples, for non-indexed track %d, violating Section 6.2.3.2. of ISO/IEC 23009-1:2012(E): every access unit of the non-indexed streams shall be a SAP of type 1.\n",nonSyncSamples,nonSyncSamples+syncSamples,mir->tirList[i].trackID);
       
    }

    vg.accessUnitDurationNonIndexedTrack = accessUnitDuration;  //To store for this represntation in file
    
    for(int i = 0 ; i < mir->numTIRs ; i++)
    {
        TrackInfoRec *tir = &(mir->tirList[i]);

        if(tir->leafInfo[0].segmentIndexed)//An indexed stream
        {
            for(int j = 0 ; j < mir->numTIRs ; j++)
                if(!mir->tirList[j].leafInfo[0].segmentIndexed)   //A non-indexed stream
                {
                    TrackInfoRec *nonIndexedTir = &(mir->tirList[j]);
                    
                    for(UInt32 k = 0 ; k < tir->numLeafs ; k++)
                    {
                        LeafInfo *leaf = &tir->leafInfo[k];

                        UInt32 samplesWithLessPresentationTime = 0;

                        for(UInt32 l = leaf->firstMoofIndex ; l <= leaf->lastMoofIndex ; l++)//Process all moofs in leaf till find a sample of non indexed stream with EPT
                        {
                            MoofInfoRec *moof = &mir->moofInfo[l];
                                                        
                            for(UInt32 m = 0; m < moof->numTrackFragments ; m++)
                            {
                                if(moof->trafInfo[m].track_ID == nonIndexedTir->trackID && moof->trafInfo[m].numTrun > 0) //Assuming 'trun' cannot be empty, 14496-12 version 4 does not indicate such a possiblity.
                                {
                                    for(UInt32 n = 0 ; n < moof->trafInfo[m].numTrun ; n++)
                                    {
                                        for(UInt32 o = 0 ; o < moof->trafInfo[m].trunInfo[n].sample_count ; o++)
                                        {                                                        
                                            long double samplePresentationTime = moof->trafInfo[m].trunInfo[n].samplePresentationTime[o];

                                            if(samplePresentationTime <= leaf->earliestPresentationTime)
                                                samplesWithLessPresentationTime++;
                                                
                                        }
                                    }
                                }
                            }
                            
                        }

                        if(samplesWithLessPresentationTime != 1)
                            errprint("%d samples of the non-indexed track %d with composition time <= the indexed track %d with EPT %LF found, violating Section 6.3.4.3. of ISO/IEC 23009-1:2012(E): for each Subsegment, every non-indexed stream must contain exactly one access unit within the Subsegment with presentation time less than or equal to the earliest presentation time of the Subsegment\n",
                                                samplesWithLessPresentationTime,nonIndexedTir->trackID,tir->trackID,leaf->earliestPresentationTime);                            
                        
                    }
                    
                }
        }

       
    }

}


void verifyAlignment(MovieInfoRec *mir)
{
    if(vg.checkSegAlignment == false && vg.checkSubSegAlignment == false)
        return;
    
    if(vg.numControlTracks != (unsigned int)mir->numTIRs)
    {
        errprint("Number of tracks logged %d in alignment control file not equal to the number of indexed tracks %d for this representation\n",vg.numControlTracks,mir->numTIRs);
        return;
    }
    
    for(int i = 0 ; i < mir->numTIRs ; i++)
    {
        TrackInfoRec *tir = &(mir->tirList[i]);
        
        if(vg.numControlLeafs[i] != tir->numLeafs)
        {
            errprint("Number of leafs %d in alignment control file for track %d not equal to the number of leafs %d for this representation\n",vg.numControlLeafs[i],tir->trackID,tir->numLeafs);
            continue;
        }
            
        for(UInt32 j = 0 ; j < (tir->numLeafs-1) ; j++)
        {
            if(vg.checkSubSegAlignment || (vg.checkSegAlignment && vg.controlLeafInfo[i][j+1].firstInSegment > 0))
                if(vg.controlLeafInfo[i][j+1].earliestPresentationTime <= tir->leafInfo[j].lastPresentationTime)
                {
                    if(vg.controlLeafInfo[i][j+1].firstInSegment > 0)
                        errprint("Overlapping segment: EPT of control leaf %Lf for leaf number %d is <= the latest presentation time %Lf corresponding leaf\n",vg.controlLeafInfo[i][j+1].earliestPresentationTime,j+1,tir->leafInfo[j].lastPresentationTime);
                    else
                        errprint("Overlapping subsegment: EPT of control leaf %Lf for leaf number %d is <= the latest presentation time %Lf corresponding leaf\n",vg.controlLeafInfo[i][j+1].earliestPresentationTime,j+1,tir->leafInfo[j].lastPresentationTime);
                }
        }
        
    }
}

void verifyBSS(MovieInfoRec *mir)
{
    if(!vg.bss)
        return;

    if(mir->numTIRs != (long)vg.numControlTracks)
        errprint("Number of tracks %d is not equal to number of tracks (%d) in control info, bitstream switching is not possible.",mir->numTIRs,vg.numControlTracks);
    
    for(int i = 0 ; i < mir->numTIRs ; i++)
    {
        TrackInfoRec *tir = &(mir->tirList[i]);

        bool correspondingTrackFound = 0;
        
        for(unsigned int j = 0 ; j < vg.numControlTracks ; j++)
        {
            if(vg.trackTypeInfo[j].track_ID == tir->trackID && vg.trackTypeInfo[j].componentSubType == tir->hdlrInfo->componentSubType)
            {
                correspondingTrackFound = true;
                break;
            }
        }

        if(!correspondingTrackFound)
            errprint("No corresponding track found in control info for track ID %lu with type %s, bitstream switching is not possible: Section 7.3.3.2. of ISO/IEC 23009-1:2012(E): The track IDs for the same media content component are identical for each Representation in each Adaptation Set",tir->trackID,ostypetostr(tir->hdlrInfo->componentSubType));            
    }
    
}

void checkSegmentStartWithSAP(int startWithSAP, MovieInfoRec *mir)
{
    for(long i = 0 ;  i < mir->numTIRs ; i++)
    {
        bool segmentStarted = false;
        int segmentCount = 0;
        
        for(UInt32 j = 0 ;  j < mir->numFragments ; j++)
        {
            if(mir->moofInfo[j].firstFragmentInSegment)
            {
                segmentStarted = true;
                segmentCount++;
            }
            
            for(UInt32 k = 0 ;  k < mir->moofInfo[j].numTrackFragments ; k++)
            {
                if(mir->moofInfo[j].trafInfo[k].track_ID != mir->tirList[i].trackID)
                    continue;
                
                for(UInt32 l = 0 ;  l < mir->moofInfo[j].trafInfo[k].numTrun; l++)
                    for(UInt32 m = 0 ;  m < mir->moofInfo[j].trafInfo[k].trunInfo[l].sample_count ; m++)
                    {
                        if(segmentStarted)
                        {
                            bool sample_is_non_sync_sample = (mir->moofInfo[j].trafInfo[k].trunInfo[l].sample_flags[m] & 0x10000 >> 16) != 0;
                            int smallestSAPType = !sample_is_non_sync_sample ? 1 : mir->moofInfo[j].trafInfo[k].trunInfo[l].sap3[m] ? 3 : mir->moofInfo[j].trafInfo[k].trunInfo[l].sap4[m] ? 4 : 7;
                            if(smallestSAPType > startWithSAP)
                            {
                                if(smallestSAPType == 7)
                                    errprint("MPD startWithSAP %d, no known SAP type found for track ID %d for segement %d\n",startWithSAP,mir->tirList[i].trackID,segmentCount);
                                else
                                    errprint("MPD startWithSAP %d, while SAP type found %d (> @startWithSAP) for track ID %d for segement %d\n",startWithSAP,smallestSAPType,mir->tirList[i].trackID,segmentCount);
                            }
                        }
                        segmentStarted = false;
                    }
            }
        }
    }
}

OSErr processIndexingInfo(MovieInfoRec *mir)
{
    UInt32 i, sidxIndex;
    UInt64 absoluteOffset;
    UInt64 referenceEPT;
    UInt64 lastLeafEPT = 0;
    UInt32 leafsProcessed = 0;

    //General checks
    UInt64 segmentOffset = 0;
    
    int firstMediaSegment = vg.initializationSegment ? 1 : 0;
	sidxIndex = 0;
    for(long trackIndex = 0 ; trackIndex < mir->numTIRs ; trackIndex++)
    {
        for(i = firstMediaSegment ; i < (UInt32)vg.segmentInfoSize ; i++)
        {
            SidxInfoRec *firstSidxOfSegment = NULL;
            
            for(UInt32 j = 0 ; j < mir->numSidx ; j++)
            {
                SidxInfoRec *previousSidx = NULL;
                
                if(mir->sidxInfo[j].reference_ID != mir->tirList[trackIndex].trackID)
                    continue;
                
                for(int k = (j-1) ; k >= 0 ; k--)
                    if(mir->sidxInfo[j].reference_ID == mir->sidxInfo[k].reference_ID)
                    {
                        previousSidx = &mir->sidxInfo[k];
                    }
                                    
                if(mir->sidxInfo[j].offset >= segmentOffset && (previousSidx == NULL || previousSidx->offset < segmentOffset))
                {
                    firstSidxOfSegment = &mir->sidxInfo[j];
					sidxIndex = j;
                    break;
                }
            }

            //Non-indexed segment
            if(firstSidxOfSegment == NULL)
                continue;

            UInt32 ref_size = 0;
            long double segmentDurationSec = 0;

            for(UInt32 j = 0 ; j < mir->numFragments; j++)
            {
				/*compute size of segment*/
				if (!ref_size) {
					for (UInt32 k=0; k<firstSidxOfSegment->reference_count; k++) {
						ref_size += firstSidxOfSegment->references[k].referenced_size;
					}
				}

                if(mir->moofInfo[j].offset >= segmentOffset && mir->moofInfo[j].offset < firstSidxOfSegment->offset)
                    errprint("Section 6.3.4.3. of ISO/IEC 23009-1:2012(E): If 'sidx' is present in a Media Segment, the first 'sidx' box shall be placed before any 'moof' box. Violated for fragment number %d\n",j+1);

				/*check that segment indexing covers the entire moof's duration*/
				if (segmentOffset + ref_size < mir->moofInfo[j].offset) {
					long double diff = ABS(segmentDurationSec - firstSidxOfSegment->cumulatedDuration);		            
					if(diff > (long double)1.0/(long double)mir->tirList[trackIndex].mediaTimeScale)
						errprint("Section 6.3.4.3. of ISO/IEC 23009-1:2012(E): If 'sidx' is present in a Media Segment, the first 'sidx' box ... shall document the entire Segment. Violated for Media Segment %d. Segment duration %Lf, Sidx documents %Lf for track %d, diff %Lf\n",i-firstMediaSegment,segmentDurationSec,firstSidxOfSegment->cumulatedDuration,mir->tirList[trackIndex].trackID,diff);

					/*find next sidx*/
					for(UInt32 k = sidxIndex ; k < mir->numSidx ; k++) {
						if (mir->sidxInfo[k].offset >= segmentOffset + ref_size) {
							sidxIndex = k;
							ref_size = 0;
		                    firstSidxOfSegment = &mir->sidxInfo[k];
							segmentOffset = firstSidxOfSegment->offset;
							segmentDurationSec = 0;
							break;
						}
					}
				}

                if(mir->moofInfo[j].samplesToBePresented && mir->moofInfo[j].offset >= segmentOffset && mir->moofInfo[j].offset < (segmentOffset+vg.segmentSizes[i]) )
                {
                    segmentDurationSec += (mir->moofInfo[j].moofPresentationEndTimePerTrack[trackIndex] - mir->moofInfo[j].moofEarliestPresentationTimePerTrack[trackIndex]);
                }
            }
			if (ref_size) {                        
				long double diff = ABS(segmentDurationSec - firstSidxOfSegment->cumulatedDuration);
	            
				if(diff > (long double)1.0/(long double)mir->tirList[trackIndex].mediaTimeScale)
					errprint("Section 6.3.4.3. of ISO/IEC 23009-1:2012(E): If 'sidx' is present in a Media Segment, the first 'sidx' box ... shall document the entire Segment. Violated for Media Segment %d. Segment duration %Lf, Sidx documents %Lf for track %d, diff %Lf\n",i-firstMediaSegment,segmentDurationSec,firstSidxOfSegment->cumulatedDuration,mir->tirList[trackIndex].trackID,diff);

				segmentOffset += vg.segmentSizes[i];  
			}
        }
    }
    
    if(vg.isoLive && mir->numTIRs > 1 && !vg.msixInFtyp)
        errprint("Check failed for DASH ISO Base media file format live profile, multiple streams yet no 'msix' compatible brand, violating Section 8.4.3. of ISO/IEC 23009-1:2012(E): Media Segments containing multiple Media Components shall comply with the formats defined in 6.3.4.3, i.e. the brand 'msix'\n");        

    for(i = 0 ; i < mir->numSidx ; i++)
    {
        UInt32 j;

        absoluteOffset = mir->sidxInfo[i].first_offset + mir->sidxInfo[i].offset + mir->sidxInfo[i].size;
        referenceEPT = mir->sidxInfo[i].earliest_presentation_time;
        
        UInt32 trackIndex = getTrakIndexByID(mir->sidxInfo[i].reference_ID);
        
        if(trackIndex >= (UInt32)mir->numTIRs)
            return badAtomErr;
        
        for(j = 0 ; j < mir->sidxInfo[i].reference_count ; j++)
        {
            SidxInfoRec *sidx;
            MoofInfoRec *moof;            
            
            if(mir->sidxInfo[i].references[j].reference_type == 1)
            {
                sidx = getSidxByOffset(mir->sidxInfo,mir->numSidx,absoluteOffset);
                if(sidx == NULL)
                    errprint("Referenced sidx not found for sidx number %d at reference count %d: Offset %lld\n",i+1,j,absoluteOffset);

                if(mir->sidxInfo[i].reference_ID != sidx->reference_ID)
                    errprint("Referenced sidx reference_ID %d does not match to reference_ID %d for sidx number %d at reference count %d ; Section 8.16.3.3 of ISO/IEC 14496-12 4th edition: if this Segment Index box is referenced from a \"parent\" Segment Index box, the value of reference_ID shall be the same as the value of reference_ID of the \"parent\" Segment Index box\n",sidx->reference_ID,mir->sidxInfo[i].reference_ID,i+1,j);

                if((double)referenceEPT/(double)mir->sidxInfo[i].timescale != (double)sidx->earliest_presentation_time/(double)sidx->timescale)
                    errprint("Referenced sidx earliest_presentation_time %lf does not match to reference EPT %lf for sidx number %d at reference count %d\n",(double)sidx->earliest_presentation_time/(double)sidx->timescale,(double)referenceEPT/(double)mir->sidxInfo[i].timescale,i+1,j);
                
                if((double)((long double)mir->sidxInfo[i].references[j].subsegment_duration/(long double)mir->sidxInfo[i].timescale) != (double)sidx->cumulatedDuration)
                    errprint("Referenced sidx duration %Lf does not match to subsegment_duration %Lf for sidx number %d at reference count %d\n",sidx->cumulatedDuration,((long double)mir->sidxInfo[i].references[j].subsegment_duration/(long double)mir->sidxInfo[i].timescale),i+1,j);

				if(mir->sidxInfo[i].references[j].starts_with_SAP > 0)
					for(int k = 0 ; k < sidx->reference_count ; k++)
						if(sidx->references[k].starts_with_SAP == 0)
							errprint("Referenced sidx subsegment %d has a starts_with_SAP 0, while the starts_with_SAP of this reference (index %d of sidx %d) is set, violating Section 8.16.3.3 of ISO/IEC 14496-12 4th edition:\n",
															k,j,i);

				if(mir->sidxInfo[i].references[j].SAP_type > 0)
					for(int k = 0 ; k < sidx->reference_count ; k++)
						if((sidx->references[k].SAP_type == 0) || (sidx->references[k].SAP_type > mir->sidxInfo[i].references[j].SAP_type))
							errprint("Referenced sidx subsegment %d has a SAP_type %d while the SAP_type of this reference (index %d of sidx %d) has a SAP_type %d, violating Section 8.16.3.3 of ISO/IEC 14496-12 4th edition:\n",
															k,sidx->references[k].SAP_type,j,i+1,mir->sidxInfo[i].references[j].SAP_type);
            }
            else
            {
                UInt32 moofIndex = getMoofIndexByOffset(mir->moofInfo,mir->numFragments,absoluteOffset);
                
                TrackInfoRec *tir = &(mir->tirList[trackIndex]);
                
                if(moofIndex >= mir->numFragments)
                {
                    errprint("Referenced moof not found for sidx number %d at reference count %d: Offset %lld\n",i+1,j,absoluteOffset);
                    continue;
                }
                
                moof = &mir->moofInfo[moofIndex];

                if(!moof->samplesToBePresented)
                {
                    errprint("Sidx %d reference %d referes to a moof which has no presentable samples (after applying edits)\n",i+1,j);
                    tir->leafInfo[leafsProcessed].hasFragments = false;
                }
                else
                    tir->leafInfo[leafsProcessed].hasFragments = true;

                if(moof->compositionInfoMissingPerTrack[trackIndex])
                {
                    warnprint("Composition info of the referred moof %d for sidx %d is missing.\n",moofIndex,i);
                    continue;
                }
                
                long double leafEPT = moof->moofEarliestPresentationTimePerTrack[trackIndex];

                if((leafsProcessed > 0) && (leafEPT <= lastLeafEPT))
                {
                    warnprint("A referenced leaf has an EPT %Lf less than a previous (in decode order) leaf EPT %Lf, this is not handled yet! The following operation may be unreliable\n",leafEPT,lastLeafEPT);
                    //lastLeafEPT = leafEPT;
                    //continue;
                }

                if(leafsProcessed > 0 && mir->moofInfo[moofIndex - 1].compositionInfoMissingPerTrack[trackIndex])
                {
                    warnprint("Composition info of the moof %d for sidx %d is missing. The following operation may be unreliable\n",moofIndex-1,i);
                    //continue;
                }
                
                tir->leafInfo[leafsProcessed].earliestPresentationTime = leafEPT;
                tir->leafInfo[leafsProcessed].firstMoofIndex = moof->index;

                if(leafsProcessed > 0)
                {
                    tir->leafInfo[leafsProcessed - 1].lastPresentationTime = mir->moofInfo[moofIndex - 1].moofLastPresentationTimePerTrack[trackIndex];
                    tir->leafInfo[leafsProcessed - 1].presentationEndTime = mir->moofInfo[moofIndex - 1].moofPresentationEndTimePerTrack[trackIndex];
                    tir->leafInfo[leafsProcessed - 1].lastMoofIndex = mir->moofInfo[moofIndex - 1].index;
                }

                if(leafsProcessed == (tir->numLeafs - 1))
                {
                    tir->leafInfo[leafsProcessed].lastPresentationTime = mir->moofInfo[mir->numFragments - 1].moofLastPresentationTimePerTrack[trackIndex];
                    tir->leafInfo[leafsProcessed].presentationEndTime= mir->moofInfo[mir->numFragments - 1].moofPresentationEndTimePerTrack[trackIndex];
                    tir->leafInfo[leafsProcessed].lastMoofIndex = mir->moofInfo[mir->numFragments - 1].index;
                }
                
                tir->leafInfo[leafsProcessed].firstInSegment = leafsProcessed > 0 ? checkSegmentBoundry(mir->moofInfo[moofIndex - 1].offset, absoluteOffset) : true;

                tir->leafInfo[leafsProcessed].sidxReportedDuration = (long double)(mir->sidxInfo[i].references[j].subsegment_duration)/(long double)(mir->sidxInfo[i].timescale);
                    
                if((long double)referenceEPT/(long double)mir->sidxInfo[i].timescale != leafEPT)
                    errprint("Referenced moof earliest_presentation_time %Lf does not match to reference EPT %Lf for sidx number %d at reference count %d\n",leafEPT,(long double)referenceEPT/(long double)mir->sidxInfo[i].timescale,i+1,j);
                    
				if(mir->sidxInfo[i].references[j].SAP_type > 4)
			    {
					warnprint("Sidx %d, index %d: SAP_type %d: \"For SAPs of type 5 and 6, no specific signalling in the ISO base media file format is supported.\" The following operation may be unreliable\n",i+1,j,mir->sidxInfo[i].references[j].SAP_type);
					//continue;
			    }

                if(vg.isomain && !(mir->sidxInfo[i].references[j].SAP_type >= 1 && mir->sidxInfo[i].references[j].SAP_type <= 3))
                    errprint("SAP type %d found for sidx %d, reference %d, violating Section 8.5.3. of ISO/IEC 23009-1:2012(E): At least one SAP of type 1 to 3, inclusive, shall be present for each track in each Subsegment\n",mir->sidxInfo[i].references[j].SAP_type,i+1,j);

                if(mir->sidxInfo[i].references[j].SAP_type > 0 || mir->sidxInfo[i].references[j].starts_with_SAP > 0)
                {                    
                    long double SAP_time = (long double)(mir->sidxInfo[i].references[j].SAP_delta_time + referenceEPT)/(long double)mir->sidxInfo[i].timescale;

                    long double samplePresentationTime;
                    bool SAPFound = false;
                    bool checkStartWithSAP = true;
                    
                    for(UInt32 k = 0; k < moof->numTrackFragments ; k++)
                        if(moof->trafInfo[k].track_ID == mir->sidxInfo[i].reference_ID && moof->trafInfo[k].numTrun > 0)//Assuming 'trun' cannot be empty, 14496-12 version 4 does not indicate such a possiblity.
                        {
                            for(UInt32 l = 0 ; l < moof->trafInfo[k].numTrun ; l++)
                            {
                                for(UInt32 m = 0 ; m < moof->trafInfo[k].trunInfo[l].sample_count ; m++)
                                {
                                    samplePresentationTime = moof->trafInfo[k].trunInfo[l].samplePresentationTime[m];

                                    bool sample_is_non_sync_sample = (moof->trafInfo[k].trunInfo[l].sample_flags[m] & 0x10000 >> 16) != 0;
                                    UInt8 SAP_type = mir->sidxInfo[i].references[j].SAP_type;
                                    bool sample_is_SAP = mir->sidxInfo[i].references[j].SAP_type > 0 ? ((SAP_type == 1 || SAP_type == 2) && !sample_is_non_sync_sample) || (SAP_type == 3 && moof->trafInfo[k].trunInfo[l].sap3[m]) || (SAP_type == 4 && moof->trafInfo[k].trunInfo[l].sap4[m]) :
                                            !sample_is_non_sync_sample || moof->trafInfo[k].trunInfo[l].sap3[m] || moof->trafInfo[k].trunInfo[l].sap4[m];   //The latter case is with starts_with_SAP > 0 and unknown SAP type (0)
                                    
                                    if(samplePresentationTime == SAP_time)
                                    {
                                        if(!sample_is_SAP)
                                        {
                                            errprint("SAP_type %d specified but the corresponding sample is not a sync sample, for sidx number %d at reference count %d\n",(int)SAP_type,i+1,j);
                                        }
                                        
                                        SAPFound = true;
                                        //printf("SAP found with presentation time %Lf \n",samplePresentationTime);
                                    }

                                    if((samplePresentationTime < SAP_time) && sample_is_SAP)
                                        errprint("SAP found with presentation time %Lf lesser than the declared SAP time %Lf (SAP_delta_time %Lf), for sidx number %d at reference count %d; first SAP shall be signaled as per Section 8.16.3.3 of ISO/IEC 14496-12 4th edition\n",samplePresentationTime,SAP_time,(long double)(mir->sidxInfo[i].references[j].SAP_delta_time)/(long double)mir->sidxInfo[i].timescale,i+1,j);
                                                                        
                                    if(SAPFound == true)
                                        break;
                                    
                                    if(mir->sidxInfo[i].references[j].starts_with_SAP > 0 && checkStartWithSAP)
                                    {
                                        errprint("starts_with_SAP declared but the first sample is not a SAP, for sidx number %d at reference count %d (checking sample %d of trun %d, traf %d, moof %d)\n",i+1,j,m+1,l+1,k+1,moofIndex+1);
                                        checkStartWithSAP = false;
                                    }
                                }
                                
                                if(SAPFound == true)
                                    break;
                            }
                                                        
                            if(SAPFound == true)
                                break;
                        }

                    if(SAPFound != true)
                        errprint("SAP not found for sidx number %d at reference count %d\n",i+1,j);
    				
                }

                lastLeafEPT = leafEPT;

                leafsProcessed ++;

            }
            
            absoluteOffset+=mir->sidxInfo[i].references[j].referenced_size;
            referenceEPT+=mir->sidxInfo[i].references[j].subsegment_duration;
                
        }
        
    }

    if(vg.startWithSAP > 0)
        checkSegmentStartWithSAP(vg.startWithSAP,mir);

    checkNonIndexedSamples(mir);
    verifyLeafDurations(mir);        
    verifyAlignment(mir);
    verifyBSS(mir);
    
	return noErr;
    
}


