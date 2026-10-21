"""Optional neutral connection preparation before an authorized input action."""
from __future__ import annotations

from tools.device.bounded_input_batch import BoundedInputBatch


def prepare_controller_connection(batch: BoundedInputBatch, *, settle_s: float = 1.2) -> None:
    """Release A, wait within the same lease, then allow the planned input.

    sys-botbase initializes its virtual controller on release as well as press.
    Keeping connection setup separate from the first pressed state can avoid a
    short first input being missed. The query is only a wire barrier; callers
    must still prove the intended page or receipt, without retrying the action.
    """
    if isinstance(settle_s, bool) or not isinstance(settle_s, (int, float)) or not .5 <= settle_s <= 3:
        raise ValueError("connection settling time outside supported range")
    batch.guard()
    if batch.input_started or batch.actions:
        raise RuntimeError("connection preparation requires a fresh input batch")
    if batch.clock() + settle_s + .4 >= batch.deadline:
        raise TimeoutError("connection preparation exceeds batch deadline")
    batch.event("controller_connection_started", settle_s=settle_s)
    # Mark before the ambiguous send so the existing context manager owns
    # detach even if no pressed state is ever sent. This never presses A.
    batch.input_started = True
    try:
        batch.client.release("A")
        if not batch.client.get_system_language():
            raise RuntimeError("empty connection barrier")
        batch.wait(settle_s)
        batch.event("controller_connection_ready", acknowledgement="none")
    except Exception:
        batch.failed = True
        batch.event("batch_stopped", reason="controller_connection_failed")
        raise
