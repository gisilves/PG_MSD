import socket
import time
import struct


def send_words_udp(file_path, ip, port, delay=0):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        with open(file_path, 'r') as file:
            while True:
                for line in file:
                    words = line.split()
                    for word in words:
                        print(f"Sending {word} to {ip}:{port}")
                        val = int(word, 16)
                        packed_data = struct.pack('>I', val)
                        sock.sendto(packed_data, (ip, port))
                        time.sleep(delay)
                # Go back to the beginning of the file
                file.seek(0)            
    except FileNotFoundError:
        pass
    finally:
        sock.close()

if __name__ == "__main__":
    # Configuration
    TARGET_IP = "127.0.0.1"
    TARGET_PORT = 8890
    FILE_NAME = "testOM_quadder.txt"
    
    send_words_udp(FILE_NAME, TARGET_IP, TARGET_PORT)
