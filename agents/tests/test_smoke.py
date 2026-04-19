"""Smoke imports so CI verifies the package installs."""

def test_import_debugger() -> None:
    from retroportal.debugger import MapFileIndex  # noqa: PLC0415

    assert MapFileIndex is not None


def test_import_binary_scanner() -> None:
    from retroportal.input import BinaryPatternScanner  # noqa: PLC0415

    assert BinaryPatternScanner is not None


def test_import_autolaunch() -> None:
    from retroportal.autolaunch import AutoLauncher  # noqa: PLC0415

    assert AutoLauncher is not None
