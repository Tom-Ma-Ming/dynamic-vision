"""Preserve already-verified run inputs without rewriting the original manifest."""
import argparse
import hashlib
import json
from pathlib import Path


def archive(root):
    manifest_path=root/'manifest.json'
    raw=manifest_path.read_bytes()
    manifest=json.loads(raw.decode('utf-8-sig'))
    directory=root/'input-archive'
    directory.mkdir(exist_ok=False)
    entries=[]
    for index,entry in enumerate(manifest['inputs']):
        source=Path(entry['path'])
        target=directory/f'{index}-{source.name}'
        digest=hashlib.sha256()
        with source.open('rb') as reader,target.open('xb') as writer:
            for chunk in iter(lambda:reader.read(1024*1024),b''):
                digest.update(chunk);writer.write(chunk)
        if digest.hexdigest().lower()!=entry['sha256'].lower():
            raise ValueError(f'Input already changed; archive not certified: {source}')
        entries.append(dict(source_path=entry['path'],file=str(target.relative_to(root)),sha256=entry['sha256']))
    result=dict(original_manifest_sha256=hashlib.sha256(raw).hexdigest(),inputs=entries)
    with (root/'input-archive.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run',type=Path)
    args=parser.parse_args()
    print(json.dumps(archive(args.run),indent=2))
