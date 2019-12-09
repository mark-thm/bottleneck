from ..src.bn_template import make_c_files
import os


def test_make_c_files():
    dirpath = os.path.join(os.path.dirname(__file__), "data/template_test")
    modules = ["test"]
    test_input = os.path.join(dirpath, "test.c")
    if os.path.exists(test_input):
        os.remove(test_input)

    make_c_files(dirpath=dirpath, modules=modules)

    with open(os.path.join(dirpath, "truth.c")) as f:
        truth = f.read()

    with open(os.path.join(dirpath, "test.c")) as f:
        test = f.read()
    test = test.replace(dirpath, "{DIRPATH}")

    assert truth == test

    os.remove(test_input)
