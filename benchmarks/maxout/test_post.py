import socket

body = b'{"author":"zz","content":"cc"}'
req = (b'POST /posts HTTP/1.1\r\nHost: x\r\nContent-Length: ' +
       str(len(body)).encode() + b'\r\n\r\n' + body)
s = socket.create_connection(('127.0.0.1', 8081))
s.sendall(req)
print(s.recv(4096).decode())
s.close()
