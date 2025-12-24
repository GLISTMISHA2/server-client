import socket
HOST = '127.0.0.1'
PORT = 5000
msg = 'привет от клиента'
s = socket.socket()
s.connect((HOST, PORT))
s.sendall(msg.encode())
data = s.recv(1024)
print('получено с сервера:', data.decode())
s.close()
print()