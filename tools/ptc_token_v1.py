#!/usr/bin/env python3
"""V1 offline grant token helpers.

This module is intentionally dependency-free so it can be used by CLI tools,
fixture generation, and early host-side tests before the C implementation
exists.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import hmac
import re


TOKEN_VERSION = 1
TOKEN_ACTION_ADD_TODAY_MINUTES = 1
TOKEN_DOMAIN = b"PTC1"
TOKEN_SYMBOLS = 20
PAYLOAD_BITS = 60
MAC_BITS = 40
TOTAL_BITS = 100
MAX_MINUTES = 1440
MAX_NONCE = (1 << 25) - 1
MAX_DAY_INDEX = (1 << 16) - 1

CROCKFORD_ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
DECODE_ALIASES = {
    "O": "0",
    "o": "0",
    "I": "1",
    "i": "1",
    "L": "1",
    "l": "1",
}


class TokenError(ValueError):
    def __init__(self, reason: str, message: str):
        super().__init__(message)
        self.reason = reason


@dataclass(frozen=True)
class TokenPayload:
    version: int
    action: int
    minutes: int
    day_index_since_2020: int
    nonce: int


def _validate_payload(payload: TokenPayload) -> None:
    if payload.version != TOKEN_VERSION:
        raise TokenError("bad_token_version", "unsupported token version")
    if payload.action != TOKEN_ACTION_ADD_TODAY_MINUTES:
        raise TokenError("unsupported_action", "unsupported token action")
    if not 1 <= payload.minutes <= MAX_MINUTES:
        raise TokenError("bad_code", "minutes out of range")
    if not 0 <= payload.day_index_since_2020 <= MAX_DAY_INDEX:
        raise TokenError("bad_code", "day index out of range")
    if not 0 <= payload.nonce <= MAX_NONCE:
        raise TokenError("bad_code", "nonce out of range")


def pack_payload(payload: TokenPayload) -> int:
    _validate_payload(payload)
    value = payload.version & 0xF
    value = (value << 4) | (payload.action & 0xF)
    value = (value << 11) | (payload.minutes & 0x7FF)
    value = (value << 16) | (payload.day_index_since_2020 & 0xFFFF)
    value = (value << 25) | (payload.nonce & MAX_NONCE)
    return value


def unpack_payload(value: int) -> TokenPayload:
    nonce = value & MAX_NONCE
    value >>= 25
    day_index = value & 0xFFFF
    value >>= 16
    minutes = value & 0x7FF
    value >>= 11
    action = value & 0xF
    value >>= 4
    version = value & 0xF
    payload = TokenPayload(version, action, minutes, day_index, nonce)
    _validate_payload(payload)
    return payload


def _payload_bytes(payload_value: int) -> bytes:
    return payload_value.to_bytes(8, "big")[-8:]


def calculate_mac(payload_value: int, device_id: str, secret: str) -> int:
    msg = TOKEN_DOMAIN + device_id.encode("utf-8") + b"\0" + _payload_bytes(payload_value)
    digest = hmac.new(secret.encode("utf-8"), msg, hashlib.sha256).digest()
    return int.from_bytes(digest[:5], "big")


def encode_base32(value: int) -> str:
    if not 0 <= value < (1 << TOTAL_BITS):
        raise TokenError("bad_code", "token integer out of range")
    chars = []
    for shift in range(TOTAL_BITS - 5, -1, -5):
        chars.append(CROCKFORD_ALPHABET[(value >> shift) & 0x1F])
    return "-".join("".join(chars[i : i + 5]) for i in range(0, TOKEN_SYMBOLS, 5))


def decode_base32(code: str) -> int:
    normalized = re.sub(r"[\s-]", "", code)
    if len(normalized) != TOKEN_SYMBOLS:
        raise TokenError("bad_code", "token must contain 20 Crockford Base32 symbols")

    value = 0
    for ch in normalized:
        ch = DECODE_ALIASES.get(ch, ch).upper()
        idx = CROCKFORD_ALPHABET.find(ch)
        if idx < 0:
            raise TokenError("bad_code", "token contains an invalid symbol")
        value = (value << 5) | idx
    return value


def encode_token(payload: TokenPayload, device_id: str, secret: str) -> str:
    payload_value = pack_payload(payload)
    mac = calculate_mac(payload_value, device_id, secret)
    token_value = (payload_value << MAC_BITS) | mac
    return encode_base32(token_value)


def decode_token(code: str, device_id: str, secret: str) -> TokenPayload:
    token_value = decode_base32(code)
    payload_value = token_value >> MAC_BITS
    actual_mac = token_value & ((1 << MAC_BITS) - 1)
    expected_mac = calculate_mac(payload_value, device_id, secret)
    if actual_mac != expected_mac:
        raise TokenError("bad_signature", "token signature does not match")
    return unpack_payload(payload_value)


def verify_token(
    code: str,
    device_id: str,
    secret: str,
    current_day_index: int,
    max_add_minutes: int,
    used_nonces: set[tuple[int, int]] | None = None,
) -> TokenPayload:
    payload = decode_token(code, device_id, secret)
    if payload.day_index_since_2020 != current_day_index:
        raise TokenError("wrong_date", "token is not valid for the current day")
    if payload.minutes > max_add_minutes:
        raise TokenError("minutes_exceed_limit", "token minutes exceed configured maximum")
    if used_nonces is not None and (payload.day_index_since_2020, payload.nonce) in used_nonces:
        raise TokenError("used_token", "token nonce was already used")
    return payload

