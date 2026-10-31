"""Independent exact phrase-oracle regression, including document-level summaries.

This checks anchor/AC partition semantics, not the production tokenizer.
"""
from collections import Counter, defaultdict
from random import Random


def summaries(matches):
    return [(len(ids), sum(ids), ids[:10]) for ids in matches]


def exhaustive(phrases, documents):
    matched = [[] for _ in phrases]
    for docid, tokens in enumerate(documents):
        for qid, phrase in enumerate(phrases):
            for i in range(len(tokens) - len(phrase) + 1):
                if tokens[i:i + len(phrase)] == phrase:
                    matched[qid].append(docid)
                    break
    return summaries(matched)


def hybrid(phrases, documents, cutoff=8):
    frequency = Counter(w for query in phrases for w in query)
    anchored = defaultdict(list)
    fallback = []
    for qid, query in enumerate(phrases):
        pos = min(range(len(query)), key=lambda j: frequency[query[j]])
        if frequency[query[pos]] <= cutoff:
            anchored[query[pos]].append((qid, pos))
        else:
            fallback.append(qid)
    matched = [[] for _ in phrases]
    for docid, tokens in enumerate(documents):
        found = set()
        for p, word in enumerate(tokens):
            for qid, j in anchored[word]:
                phrase = phrases[qid]
                start = p - j
                if qid not in found and start >= 0 and start + len(phrase) <= len(tokens):
                    if tokens[start:start + len(phrase)] == phrase:
                        found.add(qid)
        for qid in fallback:
            phrase = phrases[qid]
            if any(tokens[i:i + len(phrase)] == phrase
                   for i in range(len(tokens) - len(phrase) + 1)):
                found.add(qid)
        for qid in found:
            matched[qid].append(docid)
    return summaries(matched)


def main():
    rng = Random(20261031)
    checks = 0
    for trial in range(25):
        phrases = []
        for i in range(120):
            if i and i % 9 == 0:
                phrases.append(phrases[i - 1][:])
            else:
                n = 1 + rng.randrange(8)
                phrases.append([rng.randrange(12) if rng.randrange(3) else
                                20 + rng.randrange(1000) for _ in range(n)])
        documents = []
        for docid in range(75):
            row = [rng.randrange(12) if rng.randrange(3) else
                   20 + rng.randrange(1000) for _ in range(60)]
            if docid % 5 == 0:
                phrase = phrases[rng.randrange(len(phrases))]
                pos = rng.randrange(len(row) - len(phrase) + 1)
                row[pos:pos + len(phrase)] = phrase
                if docid % 10 == 0:
                    pos = rng.randrange(len(row) - len(phrase) + 1)
                    row[pos:pos + len(phrase)] = phrase
            documents.append(row)
        gold = exhaustive(phrases, documents)
        assert hybrid(phrases, documents) == gold
        checks += len(phrases) * len(documents)
    print(f"PASS: {checks:,} phrase/document pairs; exact count/sum/first-ten IDs")


if __name__ == "__main__":
    main()
