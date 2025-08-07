import socket
import threading

def connect_and_send(server_address):
    try:
        # Create a non-blocking socket
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setblocking(False)
        s.connect_ex(server_address)

        request = "GET / HTTP/1.1\r\nHost: localhost:8080\r\nConnection: keep-alive\r\n\r\n"
        s.sendall(request.encode())

        response = b''
        while True:
            try:
                data = s.recv(1024)
                if not data:
                    break
                response += data
            except BlockingIOError:
                continue

        print(f"Received response: {response.decode()}")
        s.close()
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    server_address = ('localhost', 8080)
    num_connections = 30

    threads = []
    for _ in range(num_connections):
        thread = threading.Thread(target=connect_and_send, args=(server_address,))
        threads.append(thread)
        thread.start()

    for thread in threads:
        thread.join()

    print("All connections processed.")
