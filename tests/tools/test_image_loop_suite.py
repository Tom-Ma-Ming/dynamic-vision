import importlib.util
from pathlib import Path
import pytest

spec = importlib.util.spec_from_file_location('image_suite', Path(__file__).parents[2]/'tools/run_image_loop_suite.py')
suite = importlib.util.module_from_spec(spec)
spec.loader.exec_module(suite)


def test_rectangle_formats_and_clipping():
    for points in ([[8, 9], [-1, -2]], [[-1, -2], [8, -2], [8, 9], [-1, 9]]):
        assert suite.rectangle({'shape_type': 'rectangle', 'points': points}, 6, 7) == [0, 0, 6, 7]


def test_invalid_shapes_rejected():
    for shape in ({'shape_type': 'polygon', 'points': [[0, 0], [3, 3]]},
                  {'shape_type': 'rectangle', 'points': [[0, 0], [float('nan'), 3]]},
                  {'shape_type': 'rectangle', 'points': [[9, 9], [10, 10]]}):
        with pytest.raises(ValueError):
            suite.rectangle(shape, 5, 5)
