import socket, struct
import numpy as np
import cv2

MAGIC = 0xA55AA55A
TYPE_IMAGE = 1
TYPE_RESULT = 2
FMT_RGB565 = 1

def recvn(conn, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed")
        buf += chunk
    return buf

def rgb565_to_bgr(img565: bytes, w: int, h: int):
    a = np.frombuffer(img565, dtype=np.uint16).reshape((h, w))
    r = ((a >> 11) & 0x1F).astype(np.uint16)
    g = ((a >> 5) & 0x3F).astype(np.uint16)
    b = (a & 0x1F).astype(np.uint16)
    r8 = (r * 255 // 31).astype(np.uint8)
    g8 = (g * 255 // 63).astype(np.uint8)
    b8 = (b * 255 // 31).astype(np.uint8)
    return np.dstack([b8, g8, r8])

def send_result(conn, frame_seq: int, okng: int, boxes):
    # payload: [frame_seq:4][okng:1][num_boxes:1][rsv:2] + N*[x,y,w,h,cls,score]
    payload = struct.pack(">I B B H", frame_seq, okng & 0xFF, len(boxes) & 0xFF, 0)
    for (x, y, w, h, cls, score01) in boxes[:32]:
        s = int(max(0.0, min(1.0, float(score01))) * 255)
        payload += struct.pack(">HHHHBB", x, y, w, h, cls & 0xFF, s & 0xFF)

    header = struct.pack(">I H H I I", MAGIC, TYPE_RESULT, 0, frame_seq, len(payload))
    conn.sendall(header + payload)

def main():
    host = "0.0.0.0"
    port = 6000
    print(f"Listening on {host}:{port}")

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(1)

    conn, addr = srv.accept()
    print("STM32 connected:", addr)

    try:
        while True:
            hdr = recvn(conn, 16)
            magic, mtype, rsv, seq, length = struct.unpack(">I H H I I", hdr)
            if magic != MAGIC:
                continue
            payload = recvn(conn, length)

            if mtype != TYPE_IMAGE:
                continue

            w, h, fmt, _ = struct.unpack(">H H B B", payload[:6])
            if fmt != FMT_RGB565:
                continue
            img565 = payload[6:]
            if len(img565) != w * h * 2:
                continue

            bgr = rgb565_to_bgr(img565, w, h)

            # TODO: replace with YOLOv8 inference:
            # boxes = [(x,y,w,h,cls,score01), ...]
            boxes = []
            okng = 1

            cv2.imshow("capture", bgr)
            if cv2.waitKey(1) == 27:
                break

            send_result(conn, seq, okng, boxes)
            print(f"Processed frame seq={seq}, returned okng={okng}, boxes={len(boxes)}")

    except Exception as e:
        print("Error:", e)
    finally:
        conn.close()
        srv.close()

if __name__ == "__main__":
    main()

