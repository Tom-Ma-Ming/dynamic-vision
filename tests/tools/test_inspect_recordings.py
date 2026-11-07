import importlib.util
from pathlib import Path

import cv2
import numpy as np
import pytest

spec = importlib.util.spec_from_file_location('inspect_recordings', Path(__file__).parents[2]/'tools/inspect_recordings.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def test_decode_counts_and_duplicate_run(tmp_path):
    video = tmp_path/'sample.avi'
    writer = cv2.VideoWriter(str(video),cv2.VideoWriter_fourcc(*'MJPG'),30,(32,32))
    assert writer.isOpened()
    for value in (0,0,0,100,200):
        writer.write(np.full((32,32,3),value,dtype=np.uint8))
    writer.release()
    result = module.inspect(video,tmp_path)
    assert result['decoded_frames'] == result['advertised_frames'] == 5
    assert result['adjacent_identical_pairs'] == 2
    assert result['longest_identical_run_frames'] == 3
    assert result['nonincreasing_media_timestamps'] == 0
    assert result['capture_timestamps_available'] is False
    assert Path(result['contact_sheet']).is_file()


def test_invalid_video_rejected(tmp_path):
    with pytest.raises(ValueError):
        module.inspect(tmp_path/'missing.avi',tmp_path)
