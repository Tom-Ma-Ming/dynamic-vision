import importlib.util
import json
from pathlib import Path
import pytest

spec=importlib.util.spec_from_file_location('reid_audit',Path(__file__).parents[2]/'tools/audit_reid_labels.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)


def fixture(root,duplicate=False):
    root.mkdir()
    mapping=[]
    for frame in range(4):
        mapping.append(dict(source_frame=frame,image=f'{frame}.png'))
        shape=dict(label='person',group_id=1,shape_type='rectangle',points=[[0,0],[10,20]])
        shapes=[shape] if frame in (0,3) else []
        if duplicate and frame==0:shapes.append(shape)
        (root/f'{frame}.json').write_text(json.dumps(dict(shapes=shapes)))
    (root/'manifest.json').write_text(json.dumps(dict(fps=2,source_video='test.avi',mapping=mapping)))


def test_gap_inventory_does_not_merge_clip_ids(tmp_path):
    for name in ('clip01','clip02'):fixture(tmp_path/name)
    a=audit.inventory(tmp_path/'clip01');b=audit.inventory(tmp_path/'clip02')
    assert a['tracklets'][0]['tracklet']!=b['tracklets'][0]['tracklet']
    row=a['tracklets'][0]
    assert row['annotations']==2 and row['long_gap_candidates']==1
    assert row['annotation_gaps'][0]['missing_frames']==2
    assert row['height_below_32']==2


def test_duplicate_identity_rejected(tmp_path):
    fixture(tmp_path/'clip',True)
    with pytest.raises(ValueError):audit.inventory(tmp_path/'clip')


def test_zero_gap_threshold_rejected(tmp_path):
    fixture(tmp_path/'clip')
    with pytest.raises(ValueError):audit.inventory(tmp_path/'clip',0)


def test_relationships_start_unknown_even_for_different_labels():
    clips=[dict(tracklets=[dict(tracklet='clip:1',source_frames=[1,2]),
                           dict(tracklet='clip:2',source_frames=[4,5]),
                           dict(tracklet='clip:3',source_frames=[2,3])])]
    review=audit.relationship_template(clips)
    assert len(review['relations'])==3
    assert all(pair['relation']=='unknown' and pair['scene_boundary_between'] is None for pair in review['relations'])
    assert review['relations'][1]['shared_frames']==[2]
