#!/usr/bin/env python3
"""Dependency-free helpers for 8-digit v2 offline grant codes."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import hmac


TOKEN_VERSION = 2
TOKEN_DOMAIN = b"PTC2"
TOKEN_LENGTH = 8
TIER_MINUTES = 5
TIER_COUNT = 32
MAX_MINUTES = 240
MAX_NONCE = (1 << 9) - 1
MAC_BITS = 12
MAX_VALUE = (1 << 26) - 1


class TokenError(ValueError):
    def __init__(self, reason: str, message: str):
        super().__init__(message)
        self.reason = reason


@dataclass(frozen=True)
class TokenV2Payload:
    tier_index: int
    minutes: int
    nonce: int


def tier_for_minutes(minutes: int) -> int:
    if 1 <= minutes <= 4:
        return 23 + minutes
    if TIER_MINUTES <= minutes <= 120 and minutes % TIER_MINUTES == 0:
        return minutes // TIER_MINUTES - 1
    if minutes == 150:
        return 28
    if minutes == 180:
        return 29
    if minutes == 210:
        return 30
    if minutes == 240:
        return 31
    raise TokenError("bad_code", "tier minutes must be 1-4, a multiple of 5 from 5 through 120, or 150/180/210/240")


def minutes_for_tier(tier_index: int) -> int:
    if 0 <= tier_index < 24:
        return (tier_index + 1) * TIER_MINUTES
    if 24 <= tier_index <= 27:
        return tier_index - 23
    if tier_index == 28:
        return 150
    if tier_index == 29:
        return 180
    if tier_index == 30:
        return 210
    if tier_index == 31:
        return 240
    raise TokenError("bad_code", "tier index out of range")


def calculate_mac(device_id: str, secret: str, day_index: int, tier_index: int, nonce: int) -> int:
    if not 0 <= day_index <= 0xFFFF:
        raise TokenError("bad_code", "day index out of range")
    if not 0 <= nonce <= MAX_NONCE:
        raise TokenError("bad_code", "nonce out of range")
    minutes_for_tier(tier_index)
    message = (
        TOKEN_DOMAIN
        + device_id.encode("utf-8")
        + b"\0"
        + day_index.to_bytes(2, "big")
        + tier_index.to_bytes(1, "big")
        + nonce.to_bytes(2, "big")
    )
    digest = hmac.new(secret.encode("utf-8"), message, hashlib.sha256).digest()
    return int.from_bytes(digest[:2], "big") >> (16 - MAC_BITS)


def encode_token(tier_index: int, nonce: int, device_id: str, secret: str, day_index: int) -> str:
    mac = calculate_mac(device_id, secret, day_index, tier_index, nonce)
    value = (tier_index << 21) | (nonce << 12) | mac
    return f"{value:08d}"


def decode_token(code: str, device_id: str, secret: str, current_day_index: int) -> TokenV2Payload:
    if len(code) != TOKEN_LENGTH or not code.isascii() or not code.isdigit():
        raise TokenError("bad_code", "v2 token must contain exactly 8 ASCII digits")
    value = int(code, 10)
    if value > MAX_VALUE:
        raise TokenError("bad_code", "v2 token exceeds the 26-bit encoding range")
    tier_index = value >> 21
    nonce = (value >> MAC_BITS) & MAX_NONCE
    actual_mac = value & ((1 << MAC_BITS) - 1)
    minutes = minutes_for_tier(tier_index)
    expected_mac = calculate_mac(device_id, secret, current_day_index, tier_index, nonce)
    if not hmac.compare_digest(actual_mac.to_bytes(2, "big"), expected_mac.to_bytes(2, "big")):
        raise TokenError("bad_signature", "token signature does not match")
    return TokenV2Payload(tier_index=tier_index, minutes=minutes, nonce=nonce)


def verify_token(
    code: str,
    device_id: str,
    secret: str,
    current_day_index: int,
    max_add_minutes: int,
    used_nonces: set[tuple[int, int]] | None = None,
) -> TokenV2Payload:
    payload = decode_token(code, device_id, secret, current_day_index)
    if payload.minutes > max_add_minutes:
        raise TokenError("minutes_exceed_limit", "token minutes exceed configured maximum")
    if used_nonces is not None and (current_day_index, payload.nonce) in used_nonces:
        raise TokenError("used_token", "token nonce was already used")
    return payload
