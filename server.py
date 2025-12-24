import socket
HOST = '127.0.0.1'
PORT = 5000
s = socket.socket()
s.bind((HOST, PORT))
s.listen(1)
print(f"слушает на {HOST}:{PORT}")
conn, addr = s.accept()
print('подключен к ', addr)
data = conn.recv(1024)
if data:
    print('получен:', data.decode())
    conn.sendall("привет от сервера".encode())
conn.close()
s.close()