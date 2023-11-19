from enum import Enum
import socket
import struct
import select

class ECommandType(Enum):
    CMD_RESPONSE = 0
    CMD_RESPONSE_FAILED = 1
    CMD_PING = 2
    CMD_GET_CERT_FOR_TITLE = 3
    CMD_COMPLETE_CHALLENGE = 4

class NXNetAuthClient:
    def __init__(self, addr: str, port: int = 7789, timeout: int = 2):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setblocking(False)
        self.addr = (addr, port)
        self.timeout = timeout

    def wait_for_response(self) -> tuple[bool, bytes]:
        ready, _, _ = select.select([self.sock], [], [], self.timeout)
        if ready:
            data, _ = self.sock.recvfrom(1024)
            return True, data
        else:
            return False, b""

    def send_command(self, cmd: int, data: bytes = b""):
        self.sock.sendto(bytes([cmd]) + data, self.addr)

    def send_ping(self) -> bool:
        self.send_command(ECommandType.CMD_PING.value)
        received_res, _ = self.wait_for_response()
        if received_res:
            return True
        
        raise Exception("Ping failed (no response from device)")
    
    def get_cert_for_title(self, title_id: int) -> bytes:
        self.send_command(ECommandType.CMD_GET_CERT_FOR_TITLE.value, struct.pack(">Q", title_id))
        received_res, data = self.wait_for_response()
        
        if not received_res:
            raise Exception("Failed to get cert for title (no response from device)")
        
        if data[0] == ECommandType.CMD_RESPONSE_FAILED.value:
            raise Exception("Failed to get cert for title (request failed, check device for more details)")
        
        if len(data) != 0x201:
            raise Exception("Failed to get cert for title (request succeeded, but response size was invalid)")
        
        return data[1:]
    
    def complete_challenge(self, value: bytes, seed: bytes) -> bytes:

        if len(value) != 16:
            raise BufferError("'value' must be 16 bytes long")
        
        if len(seed) != 15:
            raise BufferError("'seed' must be 15 bytes long")

        self.send_command(ECommandType.CMD_COMPLETE_CHALLENGE.value, value + seed)
        received_res, data = self.wait_for_response()
        
        if not received_res:
            raise Exception("Failed to complete challenge (no response from device)")
        
        if data[0] == ECommandType.CMD_RESPONSE_FAILED.value:
            raise Exception("Failed to complete challenge (request failed, check device for more details)")
        
        if len(data) != 0x59:
            raise Exception("Failed to complete challenge (request succeeded, but response size was invalid)")
        
        return data[1:]
