"""Convert the Opus-MT zh/en models into int8 CTranslate2 models under models/.

Developer-machine only; end users receive the converted directories.

    python -m pip install ctranslate2 transformers sentencepiece torch
    python tools/convert_opus_mt.py [--output models]
"""

import argparse
import pathlib
import shutil
import subprocess
import sys

MODELS = {
    "Helsinki-NLP/opus-mt-zh-en": "opus-mt-zh-en-ct2",
    "Helsinki-NLP/opus-mt-en-zh": "opus-mt-en-zh-ct2",
}


def convert(source: str, destination: pathlib.Path, force: bool) -> None:
    if (destination / "model.bin").exists() and not force:
        print(f"skip {destination} (already converted)")
        return
    if destination.exists():
        shutil.rmtree(destination)
    subprocess.run(
        [
            sys.executable, "-m", "ctranslate2.converters.transformers",
            "--model", source,
            "--output_dir", str(destination),
            "--quantization", "int8",
            "--copy_files", "source.spm", "target.spm",
        ],
        check=True,
    )
    for required in ("model.bin", "source.spm", "target.spm"):
        if not (destination / required).exists():
            sys.exit(f"{destination} is missing {required}")
    print(f"converted {source} -> {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    root = pathlib.Path(__file__).resolve().parent.parent
    parser.add_argument("--output", type=pathlib.Path, default=root / "models")
    parser.add_argument("--force", action="store_true", help="reconvert existing models")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for source, name in MODELS.items():
        convert(source, args.output / name, args.force)


if __name__ == "__main__":
    main()
