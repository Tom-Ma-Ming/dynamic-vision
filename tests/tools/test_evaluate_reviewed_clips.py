import sys
from pathlib import Path

import pytest

sys.path.insert(0,str(Path(__file__).parents[2]/'tools'))
from evaluate_reviewed_clips import detection_counts, aligned_rows
from score_mot import score


def test_one_to_one_and_empty_frames():
    box=[0,0,10,10]
    assert detection_counts([box],[box,box])==dict(tp=1,fp=1,fn=0)
    assert detection_counts([], [box])==dict(tp=0,fp=1,fn=0)
    assert detection_counts([box],[])==dict(tp=0,fp=0,fn=1)
    assert detection_counts([box],[[20,20,30,30]])==dict(tp=0,fp=1,fn=1)


def test_temporal_alignment_does_not_accept_shifted_predictions():
    entries=[dict(source_frame=1,source_ts_ns=40)]
    rows=[dict(frame_id=0,source_ts_ns=0),dict(frame_id=1,source_ts_ns=40)]
    assert list(aligned_rows(entries,rows))==[rows[1]]
    rows[1]['source_ts_ns']=80
    with pytest.raises(ValueError,match='timestamp'):list(aligned_rows(entries,rows))


def test_detection_assignment_preserves_two_valid_matches():
    gt=[[0,0,10,10],[4,0,14,10]]
    pred=[[2,0,12,10],[-2,0,8,10]]
    assert detection_counts(gt,pred)==dict(tp=2,fp=0,fn=0)


def test_identity_metrics_detect_fragmentation_with_perfect_boxes():
    gt=[1,0,0,10,10]
    perfect=[dict(gt=[gt],tracks=[[10,0,0,10,10]]) for _ in range(4)]
    result=score(perfect)
    assert result['IDF1']==1 and result['HOTA']==1 and result['IDSW']==0
    perfect[2]['tracks'][0][0]=20;perfect[3]['tracks'][0][0]=20
    fragmented=score(perfect)
    assert fragmented['IDSW']==1 and fragmented['IDF1']<1 and fragmented['HOTA']<1
