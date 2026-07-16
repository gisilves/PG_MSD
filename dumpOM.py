import socket
import sys

UDP_IP   = "127.0.0.1"
UDP_PORT = 8899
BUF_SIZE = 65535

EVENT_START = 0xfa4af1ca
BOARD_START = 0xbaba1a9a
BOARD_END   = 0x0bedface


def main():
    if len(sys.argv) != 2:
        print("Usage: python udp_dump.py <output.txt>")
        return

    out_name = sys.argv[1]

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))

    with open(out_name, "w") as f:
        while True:
            data, addr = sock.recvfrom(BUF_SIZE)

            n = len(data) // 4
            for i in range(n):
                w = int.from_bytes(
                    data[4*i:4*i+4],
                    byteorder="big",
                    signed=False
                )

                if w == EVENT_START:
                    print("EVENT_START")
                elif w == BOARD_START:
                    print("BOARD_START")
                elif w == BOARD_END:
                    print("BOARD_END")

                f.write(f"{w:08x} ")

            f.write("\n")
            f.flush()


if __name__ == "__main__":
    main()
