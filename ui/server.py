"""LightLetter UI와 검증된 QAT 체크포인트의 로컬 추론 서버.

이 경로는 macOS 사전 테스트용 PyTorch 추론이다. FPGA CNN RTL 출력이 아니다.
"""

import json
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parent
TB = ROOT.parent / "tb"
sys.path.insert(0, str(TB))
from cnn_golden.qat_sweep import SweepNet  # noqa: E402

MODEL_ID = "C2-P0-S1-F3"
RESULT = TB / "results/260914-uppercase-qat16-retrain/ByClass-Uppercase-Digits" / MODEL_ID
CONFIG = json.loads((RESULT / "result.json").read_text())["config"]
MODEL = SweepNet(CONFIG)
# 이 저장소에서 직접 학습해 생성한 신뢰된 체크포인트만 읽는다.
MODEL.load_state_dict(torch.load(RESULT / "last.pt", map_location="cpu", weights_only=False)["model"])
MODEL.eval()
CLASSES = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


class Handler(SimpleHTTPRequestHandler):
    """정적 UI와 28×28 grayscale 픽셀 추론만 localhost에 제공한다."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def do_GET(self):
        if self.path == "/api/model":
            return self.reply(200, {"model": MODEL_ID, "dataset": "ByClass-Uppercase-Digits",
                                    "qat16_test_accuracy": 0.8874686323713928,
                                    "source": "local-pytorch-qat16", "rtl": False})
        return super().do_GET()

    def do_POST(self):
        if self.path != "/api/predict":
            return self.reply(404, {"error": "unknown endpoint"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 10000:
                raise ValueError("payload too large or empty")
            data = json.loads(self.rfile.read(length))
            pixels = data["pixels"]
            if (not isinstance(pixels, list) or len(pixels) != 784 or
                    any(type(v) is not int or not 0 <= v <= 255 for v in pixels)):
                raise ValueError("expected 784 uint8 grayscale pixels")
            x = torch.tensor(pixels, dtype=torch.float32).reshape(1, 1, 28, 28) / 255
            with torch.inference_mode():
                logits = MODEL(x)[0]
                probabilities = torch.softmax(logits, dim=0)
                index = int(logits.argmax())
            self.reply(200, {"class_id": index, "char": CLASSES[index],
                             "confidence": round(float(probabilities[index]), 6),
                             "model": MODEL_ID, "source": "local-pytorch-qat16", "rtl": False})
        except (ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
            self.reply(400, {"error": str(error)})

    def reply(self, status, data):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    print(f"Local QAT model {MODEL_ID}: http://localhost:8765", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 8765), Handler).serve_forever()
