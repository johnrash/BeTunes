#!/usr/bin/env python3
# A small DAAP server for testing BeTunes' shared libraries: serves the
# audio files of a folder (and a playlist of the first two) to DAAP clients,
# with or without a password, and answers multicast DNS questions for
# _daap._tcp so BeTunes finds it. Python 3, standard library only.
#
#   daap-test-server.py --address 10.42.0.1 --dir music [--password pw]
#       [--name "Test Share"] [--port 3689] [--rate bytes/s] [--no-mdns]
#
# --address is the address to serve on and the one announced: the address
# of the network interface towards the Haiku machine. Only multicast DNS
# questions from that network (its /24) are answered: port 5353 hears
# every interface. --rate sends songs that slowly, like a share far away:
# seeking ahead then needs a Range request (206 in the log).
import argparse
import base64
import os
import socket
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

AUDIO = {".mp3": "mp3", ".m4a": "m4a", ".ogg": "ogg", ".opus": "opus",
	".flac": "flac", ".wav": "wav"}


def element(code, data):
	return code.encode() + struct.pack(">I", len(data)) + data


def number(code, value, size=4):
	return element(code, value.to_bytes(size, "big"))


def text(code, value):
	return element(code, value.encode())


def flac_duration_ms(path):
	# STREAMINFO: sample rate (20 bits) and total samples (36 bits)
	with open(path, "rb") as f:
		head = f.read(42)
	if head[:4] != b"fLaC" or len(head) < 42:
		return 0
	bits = int.from_bytes(head[18:26], "big")
	rate = bits >> 44
	samples = bits & ((1 << 36) - 1)
	return samples * 1000 // rate if rate else 0


class Library:
	def __init__(self, folder, name):
		self.name = name
		self.songs = []
		for index, entry in enumerate(sorted(os.listdir(folder))):
			path = os.path.join(folder, entry)
			stem, extension = os.path.splitext(entry)
			if extension.lower() not in AUDIO or not os.path.isfile(path):
				continue
			duration = flac_duration_ms(path) if extension.lower() == ".flac" else 0
			self.songs.append({"id": len(self.songs) + 1, "path": path,
				"title": stem.replace("-", " "), "format": AUDIO[extension.lower()],
				"size": os.path.getsize(path), "duration": duration,
				"track": len(self.songs) + 1})


class Handler(BaseHTTPRequestHandler):
	protocol_version = "HTTP/1.1"
	server_version = "daap-test-server/1.0"

	def log_message(self, form, *args):
		sys.stderr.write("daap: %s %s\n" % (self.address_string(), form % args))

	def _send(self, status, body=b"", content_type="application/x-dmap-tagged",
			headers=()):
		self.send_response(status)
		self.send_header("DAAP-Server", "daap-test-server/1.0")
		self.send_header("Content-Type", content_type)
		self.send_header("Content-Length", str(len(body)))
		for name, value in headers:
			self.send_header(name, value)
		self.end_headers()
		if self.command != "HEAD":
			self.wfile.write(body)

	def _authorized(self):
		password = self.server.password
		if not password:
			return True
		header = self.headers.get("Authorization", "")
		if not header.startswith("Basic "):
			return False
		try:
			credentials = base64.b64decode(header[6:]).decode()
		except ValueError:
			return False
		return credentials.partition(":")[2] == password

	def do_GET(self):
		url = urlparse(self.path)
		query = {k: v[0] for k, v in parse_qs(url.query).items()}
		path = url.path.rstrip("/")
		library = self.server.library
		ok = number("mstt", 200)

		if path == "/server-info":
			body = ok + number("mpro", 0x00020000) + number("apro", 0x00030000) \
				+ text("minm", library.name) \
				+ number("msau", 2 if self.server.password else 0, 1) \
				+ number("mslr", 1 if self.server.password else 0, 1) \
				+ number("mstm", 1800) + number("msdc", 1)
			return self._send(200, element("msrv", body))

		if not self._authorized():
			return self._send(401, b"", "text/plain",
				[("WWW-Authenticate", 'Basic realm="daap"')])

		if path == "/login":
			self.server.session += 1
			self.server.sessions.add(self.server.session)
			return self._send(200, element("mlog", ok
				+ number("mlid", self.server.session)))

		try:
			session = int(query.get("session-id", "0"))
		except ValueError:
			session = 0
		if session not in self.server.sessions:
			return self._send(403, b"", "text/plain")

		if path == "/logout":
			self.server.sessions.discard(session)
			return self._send(204)
		if path == "/update":
			return self._send(200, element("mupd", ok + number("musr", 2)))
		if path == "/databases":
			item = element("mlit", number("miid", 1) + text("minm", library.name)
				+ number("mimc", len(library.songs)) + number("mctc", 2))
			return self._send(200, element("avdb", ok + number("muty", 0, 1)
				+ number("mtco", 1) + number("mrco", 1) + element("mlcl", item)))
		if path == "/databases/1/items":
			items = b"".join(element("mlit", number("mikd", 2, 1)
				+ number("miid", s["id"]) + text("minm", s["title"])
				+ text("asar", "BeTunes test") + text("asal", library.name)
				+ text("asfm", s["format"]) + number("astm", s["duration"])
				+ number("assz", s["size"]) + number("astn", s["track"], 2))
				for s in library.songs)
			return self._send(200, element("adbs", ok + number("muty", 0, 1)
				+ number("mtco", len(library.songs))
				+ number("mrco", len(library.songs)) + element("mlcl", items)))
		if path == "/databases/1/containers":
			base = element("mlit", number("miid", 1) + text("minm", library.name)
				+ number("mimc", len(library.songs)) + number("abpl", 1, 1))
			playlist = element("mlit", number("miid", 2) + text("minm", "Test playlist")
				+ number("mimc", min(2, len(library.songs))))
			return self._send(200, element("aply", ok + number("muty", 0, 1)
				+ number("mtco", 2) + number("mrco", 2)
				+ element("mlcl", base + playlist)))
		if path.startswith("/databases/1/containers/") and path.endswith("/items"):
			container = path.split("/")[4]
			songs = library.songs if container == "1" else library.songs[:2][::-1]
			items = b"".join(element("mlit", number("mikd", 2, 1)
				+ number("miid", s["id"]) + number("mcti", s["id"])) for s in songs)
			return self._send(200, element("apso", ok + number("muty", 0, 1)
				+ number("mtco", len(songs)) + number("mrco", len(songs))
				+ element("mlcl", items)))
		if path.startswith("/databases/1/items/"):
			try:
				song_id = int(path.rsplit("/", 1)[1].split(".")[0])
				song = next(s for s in library.songs if s["id"] == song_id)
			except (ValueError, StopIteration):
				return self._send(404, b"", "text/plain")
			return self._send_song(song)
		return self._send(404, b"", "text/plain")

	def _send_song(self, song):
		size = song["size"]
		start, end = 0, size - 1
		status = 200
		header = self.headers.get("Range", "")
		if header.startswith("bytes="):
			first, _, last = header[6:].partition("-")
			try:
				start = int(first) if first else 0
				end = min(int(last), size - 1) if last else size - 1
			except ValueError:
				return self._send(416, b"", "text/plain")
			if start > end:
				return self._send(416, b"", "text/plain")
			status = 206
		self.send_response(status)
		self.send_header("Content-Type", "audio/" + song["format"])
		self.send_header("Content-Length", str(end - start + 1))
		self.send_header("Accept-Ranges", "bytes")
		if status == 206:
			self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
		self.end_headers()
		rate = self.server.rate
		with open(song["path"], "rb") as f:
			f.seek(start)
			left = end - start + 1
			try:
				while left > 0:
					chunk = f.read(min(16384 if rate else 65536, left))
					if not chunk:
						break
					self.wfile.write(chunk)
					left -= len(chunk)
					if rate:
						time.sleep(len(chunk) / rate)
			except (BrokenPipeError, ConnectionResetError):
				pass


def dns_name(name):
	return b"".join(bytes([len(p)]) + p for p in name.encode().split(b".")) + b"\0"


def read_question(packet):
	# the first question's name and type, uncompressed (queries are)
	offset, labels = 12, []
	while offset < len(packet) and packet[offset] != 0:
		count = packet[offset]
		labels.append(packet[offset + 1:offset + 1 + count].decode(errors="replace"))
		offset += 1 + count
	qtype = struct.unpack(">H", packet[offset + 1:offset + 3])[0] \
		if offset + 3 <= len(packet) else 0
	return ".".join(labels).lower(), qtype


def mdns_responder(address, name, port, password):
	sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	if hasattr(socket, "SO_REUSEPORT"):
		try:
			sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
		except OSError:
			pass
	sock.bind(("", 5353))
	sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
		socket.inet_aton("224.0.0.251") + socket.inet_aton(address))
	host = "daap-test-server.local"
	instance = name + "._daap._tcp.local"
	txt = b"".join(bytes([len(t)]) + t for t in [b"txtvers=1",
		b"Machine Name=" + name.encode(),
		b"Password=" + (b"true" if password else b"false")])
	sys.stderr.write("mdns: answering for \"%s\" on %s\n" % (name, address))
	network = socket.inet_aton(address)[:3]
	while True:
		packet, sender = sock.recvfrom(9000)
		if len(packet) < 12 or packet[2] & 0x80:
			continue
		if socket.inet_aton(sender[0])[:3] != network:
			continue
				# a question from another network: not ours to answer
		question, qtype = read_question(packet)
		if question != "_daap._tcp.local" or qtype not in (12, 255):
			continue
		records = [
			(dns_name("_daap._tcp.local"), 12, dns_name(instance)),
			(dns_name(instance), 33, struct.pack(">HHH", 0, 0, port) + dns_name(host)),
			(dns_name(instance), 16, txt),
			(dns_name(host), 1, socket.inet_aton(address))]
		reply = packet[:2] + struct.pack(">HHHHH", 0x8400, 0, len(records), 0, 0)
		for record_name, record_type, data in records:
			reply += record_name + struct.pack(">HHIH", record_type, 1, 120,
				len(data)) + data
		sock.sendto(reply, sender)
		sys.stderr.write("mdns: answered %s:%d\n" % sender)


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--address", required=True)
	parser.add_argument("--dir", required=True)
	parser.add_argument("--name", default="BeTunes Test Share")
	parser.add_argument("--password", default="")
	parser.add_argument("--port", type=int, default=3689)
	parser.add_argument("--rate", type=int, default=0)
	parser.add_argument("--no-mdns", action="store_true")
	args = parser.parse_args()

	server = ThreadingHTTPServer((args.address, args.port), Handler)
	server.library = Library(args.dir, args.name)
	server.password = args.password
	server.rate = args.rate
	server.session = 1000
	server.sessions = set()
	sys.stderr.write("daap: %d songs on %s:%d%s\n" % (len(server.library.songs),
		args.address, args.port, ", with a password" if args.password else ""))
	if not args.no_mdns:
		threading.Thread(target=mdns_responder, args=(args.address, args.name,
			args.port, args.password), daemon=True).start()
	server.serve_forever()


if __name__ == "__main__":
	main()
