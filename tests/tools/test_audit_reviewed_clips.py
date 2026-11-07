import importlib.util
import json
from pathlib import Path

import cv2
import numpy as np

spec = importlib.util.spec_from_file_location('audit_reviewed_clips',Path(__file__).parents[2]/'tools/audit_reviewed_clips.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture(folder,shapes):
    cv2.imwrite(str(folder/'frame.png'),np.zeros((32,32,3),np.uint8))
    (folder/'manifest.json').write_text(json.dumps({'mapping':[{'image':'frame.png','source_frame':42}]}))
    (folder/'frame.json').write_text(json.dumps(dict(imagePath='frame.png',imageWidth=32,imageHeight=32,shapes=shapes)))


def box(label,group):
    return dict(label=label,group_id=group,shape_type='rectangle',points=[[1,1],[10,10]])


def test_head_and_body_share_identity_without_collision(tmp_path):
    fixture(tmp_path,[box('person',1),box('head',1)])
    original=(tmp_path/'frame.json').read_bytes()
    result=module.audit(tmp_path)
    assert result['structural_errors']==0 and result['identities']==[1]
    assert (tmp_path/'frame.json').read_bytes()==original


def test_duplicate_person_and_missing_identity_are_rejected(tmp_path):
    fixture(tmp_path,[box('person',1),box('person',1),box('head',None)])
    result=module.audit(tmp_path)
    assert {i['kind'] for i in result['issues']}=={'duplicate_identity','identity'}


def test_outside_image_and_missing_pair_are_rejected(tmp_path):
    shape=box('head',1);shape['points'][1]=[33,10]
    fixture(tmp_path,[shape])
    assert [i['kind'] for i in module.audit(tmp_path)['issues']]==['bounds']
    (tmp_path/'frame.json').unlink()
    assert [i['kind'] for i in module.audit(tmp_path)['issues']]==['missing_pair']
