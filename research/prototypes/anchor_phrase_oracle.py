"""Exact anchored phrase matching experiment; no production code changes.

Run: python3 research/prototypes/anchor_phrase_oracle.py
"""
from collections import Counter, defaultdict
from random import Random

rng = Random(837)
common = [f"c{i}" for i in range(36)]
rare = [f"r{i}" for i in range(1500)]
queries = []
for _ in range(850):
    k = rng.randint(2, 8)
    phrase = [rng.choice(common) if rng.random() < 0.73
              else rng.choice(rare) for _ in range(k)]
    phrase[rng.randrange(k)] = rng.choice(rare)
    queries.append(tuple(phrase))

query_frequency = Counter(word for phrase in queries for word in phrase)
anchor_index = [min(range(len(phrase)),
                    key=lambda pos: (query_frequency[phrase[pos]], -len(phrase[pos])))
                for phrase in queries]
by_anchor = defaultdict(list)
for query_id, phrase in enumerate(queries):
    by_anchor[phrase[anchor_index[query_id]]].append(
        (query_id, anchor_index[query_id])
    )


def anchored_matches(tokens):
    found = set()
    comparisons = 0
    for position, token in enumerate(tokens):
        for query_id, anchor_position in by_anchor.get(token, ()):
            comparisons += 1
            phrase = queries[query_id]
            start = position - anchor_position
            if start >= 0 and start + len(phrase) <= len(tokens):
                if all(tokens[start + j] == word
                       for j, word in enumerate(phrase)):
                    found.add(query_id)
    return found, comparisons


def exhaustive_oracle(tokens):
    return {
        query_id for query_id, phrase in enumerate(queries)
        if any(tuple(tokens[i:i + len(phrase)]) == phrase
               for i in range(len(tokens) - len(phrase) + 1))
    }


total_checks = 0
match_count = 0
for document_id in range(1300):
    tokens = [rng.choice(common) if rng.random() < 0.82 else rng.choice(rare)
              for _ in range(150)]
    if document_id % 23 == 0:
        phrase = rng.choice(queries)
        pos = rng.randrange(151 - len(phrase))
        tokens[pos:pos + len(phrase)] = phrase
    found, checks = anchored_matches(tokens)
    expected = exhaustive_oracle(tokens)
    assert found == expected, (document_id, found ^ expected)
    match_count += len(found)
    total_checks += checks

assert total_checks == 19725, total_checks
assert match_count == 89, match_count
print("PASS: 1,300 documents, 195,000 tokens, 850 phrases")
print("Exact phrase results agree with independent exhaustive oracle")
print("Anchor candidate checks:", total_checks)
print("Candidate checks per input token:", round(total_checks / 195000, 4))
print("This does not measure production Aho-Corasick throughput.")
