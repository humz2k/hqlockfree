# hqlockfree

**hqlockfree** is a collection of low-latency, lock-free data-structures for C++20.

| Container       | Pattern                 | Highlights                                          |
| --------------- | ----------------------- | --------------------------------------------------- |
| `mpsc_queue`    | MPSC ring               | lock-free producers, single consumer                |
| `mpmc_fanout`   | MPMC fan-out queue      | *N* producers, *M* independent read cursors         |
| `spsc_queue`    | SPSC ring               | wait-free, cache-line padded                        |
| `spmc_push_vec` | SPMC append-only vector | producer-only `push_back`, many read-only consumers |

All algorithms avoid dynamic allocation on the hot path, isolate shared counters onto separate cache lines, and use the weakest memory ordering that is still correctness-sound.

---

## 1. Quick start

```cpp
#include <hqlockfree/mpsc_queue.hpp>

int main() {
    hqlockfree::mpsc_queue<int> q(/*min cache lines*/ 8);

    // producer threads
    std::thread p1([&]{ for (int i=0;i<1'000;i++) q.push(i); });
    std::thread p2([&]{ for (int i=0;i<1'000;i++) q.push(i+1'000); });

    // single consumer
    int v;
    while (true) {
        if (q.pop(v)) std::cout << v << '\n';
    }

    /* ... */
}
```

### Fan-out queue

```cpp
hqlockfree::mpmc_fanout<message_type> bus(32);

/* N producers */
bus.push(message);

/* Each consumer owns a handle */
auto* sub = bus.subscribe();
message_type msg;
while (sub->pop(msg)) process(msg);
```

### Append-only vector

```cpp
hqlockfree::spmc_push_vec<Order> book;
book.emplace_back(price, qty);      // producer thread only

const auto& view = book;            // consumers hold const ref
for (const auto& o : view) print(o);
```

---

## 2. Building & testing

hqlockfree is cmake-ified.

---

## 3. Contributing

See https://www.conventionalcommits.org/en/v1.0.0/ for commit messages.