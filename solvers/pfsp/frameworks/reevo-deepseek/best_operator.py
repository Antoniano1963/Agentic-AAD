def destroy_v2(sequence: list, processing_times: list) -> tuple[list, list]:
    import random

    n = len(sequence)
    if n == 0:
        return [], []
    if n == 1:
        return [], sequence[:]

    # Favor very small, mostly contiguous removals.
    # Core behavior: k = 3 or 4, occasionally 2 or 5, rare 1 or up to 6.
    r = random.random()
    if n <= 4:
        k = random.randint(1, n - 1)
    elif r < 0.72:
        k = random.randint(3, 4)
    elif r < 0.94:
        k = random.randint(2, 5)
    elif r < 0.985:
        k = 1
    else:
        k = random.randint(5, min(6, n - 1))

    if k >= n:
        k = n - 1
    if k < 1:
        k = 1

    positions = set()

    # Strongly prefer a single contiguous block. No middle bias and no jitter.
    if k == 1 or random.random() < 0.94:
        start = random.randrange(n - k + 1)
        for p in range(start, start + k):
            positions.add(p)
    else:
        # Rarely split into two very short nearby blocks.
        first_len = max(1, k // 2)
        second_len = k - first_len

        start1 = random.randrange(n - first_len + 1)
        for p in range(start1, start1 + first_len):
            positions.add(p)

        if second_len > 0:
            low = max(0, start1 - second_len - 1)
            high = min(n - second_len, start1 + first_len + 1)
            if low <= high:
                start2 = random.randint(low, high)
            else:
                start2 = random.randrange(n - second_len + 1)
            for p in range(start2, start2 + second_len):
                positions.add(p)

    # Normalize to exactly k positions.
    if len(positions) > k:
        positions = set(random.sample(list(positions), k))
    elif len(positions) < k:
        available = [p for p in range(n) if p not in positions]
        need = k - len(positions)
        positions.update(random.sample(available, need))

    if len(positions) >= n:
        positions = set(random.sample(range(n), n - 1))
    if not positions:
        positions.add(random.randrange(n))

    sorted_positions = sorted(positions, reverse=True)
    removed = [sequence[p] for p in sorted_positions]
    remaining = sequence.copy()
    for p in sorted_positions:
        remaining.pop(p)

    return remaining, removed