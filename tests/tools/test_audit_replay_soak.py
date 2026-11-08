import importlib.util
import json
import hashlib
from pathlib import Path
import pytest

spec = importlib.util.spec_from_file_location('soak_audit', Path(__file__).parents[2]/'tools/audit_replay_soak.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def good_summary():
    return dict(captured=108000,processed=108000,replaced=0,rejected=0,pending=0,
                stale=0,failed=0,peak=1,capacity=1,status='ok',mode='latest',paced=True)


def samples():
    return [dict(elapsed_s=t,pid=123,cpu_seconds=t/10,private_bytes=300*2**20,
                 working_set_bytes=200*2**20,handles=330)
            for t in list(range(300,600))+list(range(3300,3600))]


def test_valid_hour_contract():
    assert all(audit.summary_gates(good_summary(),dict(exit_code=0,elapsed_s=3600)).values())
    checks,delta,counts = audit.resource_gates(samples(),3600)
    assert all(checks.values()) and delta==0 and counts==[300,300]


@pytest.mark.parametrize('changes',[
    dict(processed=108001,replaced=-1), dict(processed=107999),
    dict(status='error'),dict(paced=False),dict(mode='blocking'),dict(capacity=2),
    dict(peak=2),dict(captured=108000.0),
])
def test_bad_summary_cannot_pass(changes):
    summary=good_summary();summary.update(changes)
    assert not all(audit.summary_gates(summary,dict(exit_code=0,elapsed_s=3600)).values())


@pytest.mark.parametrize('kind',['mixed_pid','duplicate_time','negative_memory','cpu_reversal','nan','growth','short_window'])
def test_bad_resource_evidence_cannot_pass(kind):
    rows=samples()
    if kind=='mixed_pid':rows[-1]['pid']=456
    elif kind=='duplicate_time':rows[-1]['elapsed_s']=rows[-2]['elapsed_s']
    elif kind=='negative_memory':rows[-1]['private_bytes']=-1
    elif kind=='cpu_reversal':rows[-1]['cpu_seconds']=0
    elif kind=='nan':rows[-1]['working_set_bytes']=float('nan')
    elif kind=='growth':
        for row in rows[300:]:row['private_bytes']+=33*2**20
    elif kind=='short_window':rows=rows[100:]
    checks,_,_=audit.resource_gates(rows,3600)
    assert not all(checks.values())


def test_short_run_is_not_hour_acceptance():
    assert not audit.summary_gates(good_summary(),dict(exit_code=0,elapsed_s=180))['completion']


def test_archived_inputs_survive_rebuild_but_reject_tampering(tmp_path):
    archiver_spec=importlib.util.spec_from_file_location('archive_inputs',Path(__file__).parents[2]/'tools/archive_replay_inputs.py')
    archiver=importlib.util.module_from_spec(archiver_spec);archiver_spec.loader.exec_module(archiver)
    paths=[]
    for i in range(5):
        p=tmp_path/f'input-{i}';p.write_bytes(bytes([i]));paths.append(str(p))
    manifest=dict(executable=paths[0],arguments=[paths[1],paths[2],'output',paths[3],'--config',paths[4]],
                  inputs=[dict(path=p,sha256=hashlib.sha256(Path(p).read_bytes()).hexdigest()) for p in paths])
    (tmp_path/'manifest.json').write_text(json.dumps(manifest))
    assert audit.verify_inputs(tmp_path,manifest)
    saved=archiver.archive(tmp_path)
    Path(paths[0]).write_bytes(b'rebuilt')
    assert audit.verify_inputs(tmp_path,manifest)
    copy=tmp_path/saved['inputs'][0]['file'];copy.write_bytes(b'corrupt')
    assert not audit.verify_inputs(tmp_path,manifest)


def test_cannot_certify_changed_input(tmp_path):
    archiver_spec=importlib.util.spec_from_file_location('archive_changed',Path(__file__).parents[2]/'tools/archive_replay_inputs.py')
    archiver=importlib.util.module_from_spec(archiver_spec);archiver_spec.loader.exec_module(archiver)
    source=tmp_path/'exe';source.write_bytes(b'changed')
    (tmp_path/'manifest.json').write_text(json.dumps(dict(inputs=[dict(path=str(source),sha256='0'*64)])))
    with pytest.raises(ValueError):archiver.archive(tmp_path)
    assert not (tmp_path/'input-archive.json').exists()
