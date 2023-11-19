from nintendo.switch import dauth, aauth, baas
from nintendo.nex import backend, authentication, streams, settings, matchmaking_mk8d as matchmaking, ranking_mk8d as ranking
from nintendo import switch
from nx_netauth_client import NXNetAuthClient
import anyio
import base64
import struct

import logging
logging.basicConfig(level=logging.INFO)

SWITCH_DEVICE_IP_ADDRESS = "192.168.1.91"

SYSTEM_VERSION = 1700 #17.0.0

# You can get your user id and password from
# su/baas/<guid>.dat in save folder 8000000000000010.

# Bytes 0x20 - 0x28 contain the user id in reversed
# byte order, and bytes 0x28 - 0x50 contain the
# password in plain text.

# Alternatively, you can set up a mitm on your Switch
# and extract them from the request to /1.0.0/login

BAAS_USER_ID = 0x0123456789abcdef # 16 hex digits
BAAS_PASSWORD = "..." # Should be 40 characters

# You can dump prod.keys with Lockpick_RCM and
# PRODINFO from hekate (decrypt it if necessary)
PATH_KEYS = "prod.keys"
PATH_PRODINFO = "PRODINFO.bin"

TITLE_ID = 0x0100152000022000
TITLE_VERSION = 0x110000

GAME_SERVER_ID = 0x2B309E01
ACCESS_KEY = "09c1c475"
NEX_VERSION = 40302
CLIENT_VERSION = 2


HOST = "g%08x-lp1.s.n.srv.nintendo.net" %GAME_SERVER_ID
PORT = 443

def b64url_dec(data):
	return base64.urlsafe_b64decode(data)


class ChunkData:
	def __init__(self, data: bytes, max_id: int = 12):
		self.buffer = data
		self.data: dict[int, bytes] = {}
		self.max_id = max_id

	def parse(self):
		stream = streams.StreamIn(self.buffer, ">")

		magic = stream.u16()
		if magic != 0x5a5a:
			raise ValueError("Wrong magic")

		id = stream.u8()
		while id != 255:

			if id > self.max_id:
				raise ValueError("Invalid ID")

			size = stream.u16()
			data = stream.read(size)
			self.data[id] = data
			id = stream.u8()


class TournamentMetadata:
	def __init__(self, data: bytes):
		self.chunk_data = ChunkData(data)
		self.revision = 0
		self.name = ""
		self.description = ""
		self.red_team = ""
		self.blue_team = ""
		self.repeat_type = 0
		self.gameset_num = 0
		self.icon_type = 0
		self.battle_time = 0
		self.update_date = 0
		self.version = 0

	def parse(self):
		self.chunk_data.parse()

		if self.chunk_data.data[0]:
			self.revision = struct.unpack(">B", self.chunk_data.data[0])[0]

		if self.chunk_data.data[2]:
			self.name = self.chunk_data.data[2].decode("utf-16le")[:-1]

		if self.chunk_data.data[4]:
			self.description = self.chunk_data.data[4].decode("utf-16le")[:-1]

		if self.chunk_data.data[7]:
			self.red_team = self.chunk_data.data[7].decode("utf-16le")[:-1]

		if self.chunk_data.data[8]:
			self.blue_team = self.chunk_data.data[8].decode("utf-16le")[:-1]

		if self.chunk_data.data[5]:
			self.repeat_type = struct.unpack(">I", self.chunk_data.data[5])[0]

		if self.chunk_data.data[6]:
			self.gameset_num = struct.unpack(">I", self.chunk_data.data[6])[0]

		if self.chunk_data.data[9]:
			self.battle_time = struct.unpack(">I", self.chunk_data.data[9])[0]

		if self.chunk_data.data[11]:
			self.update_date = struct.unpack(">I", self.chunk_data.data[11])[0]

		if self.chunk_data.data[3]:
			self.icon_type = struct.unpack(">B", self.chunk_data.data[3])[0]

		if self.chunk_data.data[1]:
			self.version = struct.unpack(">I", self.chunk_data.data[1])[0]

async def main():
	keys = switch.load_keys(PATH_KEYS)
	
	info = switch.ProdInfo(keys, PATH_PRODINFO)
	cert = info.get_tls_cert()
	pkey = info.get_tls_key()
	
	dauth_client = dauth.DAuthClient(keys)
	dauth_client.set_certificate(cert, pkey)
	dauth_client.set_system_version(SYSTEM_VERSION)
	
	nx_netauth_client = NXNetAuthClient(SWITCH_DEVICE_IP_ADDRESS)
	nx_netauth_client.send_ping()

	aauth_client = aauth.AAuthClient()
	aauth_client.set_system_version(SYSTEM_VERSION)
	
	baas_client = baas.BAASClient()
	baas_client.set_system_version(SYSTEM_VERSION)
	
	# Request a device authentication token for aauth and bass
	response = await dauth_client.device_token(dauth.CLIENT_ID_BAAS)
	device_token_baas = response["device_auth_token"]
	
	# Request challenge from AAUTH
	response = await aauth_client.challenge(device_token_baas)

	# Make the challenge complete by a Switch device
	cert = nx_netauth_client.get_cert_for_title(TITLE_ID)
	gvt = nx_netauth_client.complete_challenge(b64url_dec(response["value"]), b64url_dec(response["seed"]))
	
	# Request an application authentication token
	response = await aauth_client.auth_gamecard(TITLE_ID, TITLE_VERSION, device_token_baas, cert, gvt)
	app_token = response["application_auth_token"]
	
	# Request an anonymous access token for baas
	response = await baas_client.authenticate(device_token_baas)
	access_token = response["accessToken"]
	
	# Log in on the baas server
	response = await baas_client.login(
		BAAS_USER_ID, BAAS_PASSWORD, access_token, app_token
	)
	user_id = int(response["user"]["id"], 16)
	id_token = response["idToken"]

	# Set up authentication info for nex server
	auth_info = authentication.AuthenticationInfo()
	auth_info.token = id_token
	auth_info.ngs_version = 4 #Switch
	auth_info.token_type = 2
	
	s = settings.load("switch")
	s.configure(ACCESS_KEY, NEX_VERSION, CLIENT_VERSION)
	async with backend.connect(s, HOST, PORT) as be:
		async with be.login(str(user_id), auth_info=auth_info) as client:

			mm = matchmaking.MatchmakeExtensionClientMK8D(client)
			rk = ranking.RankingClientMK8D(client)

			param = ranking.CompetitionRankingInfoGetParam()
			param.range.size = 5
			param.rank_order = 0 # Descending
			top_competitions = await rk.get_competition_info(param)

			id_list = list(map(lambda c: c.id, top_competitions))
			compe_list = await mm.search_simple_search_object_by_object_ids(id_list)
			
			print()
			print("Top 5 popular competitions:")
			print("============================")

			i = 1
			for competition in compe_list:
				try:
					metadata = TournamentMetadata(competition.metadata)
					metadata.parse()
					print(f"Tournament #{i}: {metadata.name}", end="")
					if metadata.red_team:
						print(f" - Red team '{metadata.red_team}' vs Blue team '{metadata.blue_team}'")
					else:
						print()
				except:
					print(f"Tournament #{i}: (Error parsing metadata)")
				i += 1

			print()
				

anyio.run(main)
