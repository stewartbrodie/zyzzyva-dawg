/** Directed Acyclic Word Graph (DAWG)
 *
 *  This code can generate and decompile a DAWG that is compatible with those
 *  generated by Graham Toal's original C code.  This is a C++ reimagining of
 *  Graham's original, updated for modern C++, 64-bit safe, and simplified by
 *  removing support for very small memory machines.  The generated DAWG data
 *  can be used by Collins Zyzzyva as a lexicon.
 *
 *  The original algorithms and code are by Graham Toal <gtoal@gtoal.com> and
 *  released into the public domain.
 *
 *  This code is Copyright (C) Stewart Brodie, 2019
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

namespace Dawg {

namespace {
    const uint32_t MAX_CHARS = 256;

    /* MUST be prime ... pick one about 20% larger than needed */
    const size_t HASH_TABLE_SIZE  = 240007u;

    // Write out a 32-bit word to the specified stream
    std::ostream& output(std::ostream& os, uint32_t value) {
        return os.write(reinterpret_cast<char *>(&value), sizeof(uint32_t));
    }
} // namespace

struct Node {

    Node() = default;
    Node(unsigned char letter, bool ends_word) :
        value((uint32_t(letter) << letter_shift) | (ends_word ? end_of_word : 0)) {}

    bool isEndOfWord() const { return (value & end_of_word) != 0; }
    bool isEndOfNode() const { return (value & end_of_node) != 0; }
    uint32_t getOffset() const { return (value & offset_mask); }
    unsigned char getChar() const { return (value & letter_mask) >> letter_shift; }
    Node& setEndOfNode() { value |= end_of_node; return *this; }
    Node& setChildOffset(uint32_t node) { value |= (node & offset_mask); return *this; }

    bool operator==(Node const& other) const { return value == other.value; }
    void write(std::ostream& os) const { output(os, value); }
    static uint32_t hash_fn(uint32_t r, Node n) { return n.value ^ ((r << 1) | (r >> 31)); };

private:
    static constexpr uint32_t letter_mask  = 0xff000000u;
    static constexpr uint32_t end_of_word  = 0x00800000u;
    static constexpr uint32_t end_of_node  = 0x00400000u;
    static constexpr uint32_t reserve_bit  = 0x00200000u;
    static constexpr uint32_t offset_mask  = 0x001fffffu;
    static constexpr uint32_t letter_shift = 24u;

    uint32_t value { 0 };
};

struct WordBuffer {
    WordBuffer(std::istream& input) : input(input) { }

    std::pair<size_t, std::string> next() {
        std::string s;

        while ((input >> s) && s.size() < 2 ) {
            ++count;
        }

        auto search = std::mismatch(s.cbegin(), s.cend(), current.data());
        if (!s.empty() && (search.first == s.cend() || *search.first < *search.second)) {
            throw std::logic_error(std::string("Out of order strings"));
        }

        current = std::move(s);
        return std::make_pair(std::distance(s.cbegin(), search.first), current);
    }

    unsigned char operator[](size_t idx) { return current[idx]; }

private:
    size_t count { 0 };
    std::string current;
    std::istream& input;
};


struct EdgeList {
    size_t hash() const {
        return std::accumulate(edges.cbegin(), edges.cend(), uint32_t(0), Node::hash_fn) % HASH_TABLE_SIZE;
    }

    template <class T>
    bool equal(T start) const {
        return std::equal(edges.cbegin(), edges.cend(), start);
    }

    std::vector<Node> edges;
};

struct Dawg {
    Dawg()
        : dawg(MAX_CHARS) // space for the root nodes which will be filled in later
    {
        dawg.reserve(HASH_TABLE_SIZE);
        hash_table.fill(0);
    }

    void save(std::ostream&& os) {
        output(os, static_cast<uint32_t>(dawg.size()));
        for (auto const& node : dawg) {
            node.write(os);
        }
    }

    void dump(std::ostream& os) {
        try {
            std::vector<Node const *> stack { 1, &dawg[0] };

            while (!stack.empty()) {
                if (stack.back()->isEndOfWord()) {
                    for (auto const& n : stack) {
                        os << n->getChar();
                    }
                    os << "\n";
                }
                if (auto next = stack.back()->getOffset()) {
                    stack.push_back(&dawg.at(next-1)); // at() forces a range check
                }
                else {
                    while (!stack.empty() && (stack.back()++)->isEndOfNode()) {
                        stack.pop_back();
                    }
                }
            }
        }
        catch (std::out_of_range const& ex) {
            std::cerr << "DAWG appears corrupt: node pointers point outside DAWG (" << ex.what() << ")\n";
        }
    }

    void load(std::istream&& is) {
        is.seekg(0, is.end);
        auto size = is.tellg();
        is.seekg(0);

        uint32_t edges;
        is.read(reinterpret_cast<char *>(&edges), sizeof(uint32_t));
        if (edges * 4 + 4 != size) {
            std::cerr << "size is " << size << " and edges is " << edges << "\n";
            throw std::runtime_error("Input DAWG file appears to be corrupt");
        }
        dawg.resize(edges);
        is.read(reinterpret_cast<char *>(dawg.data()), edges * sizeof(uint32_t));
    }

    static const size_t hash_modulo_increment(size_t base, size_t inc) {
        base += inc;
        return (base >= HASH_TABLE_SIZE) ? base - HASH_TABLE_SIZE : base;
    }

    size_t insertEdges(EdgeList const& edges) {
        // Search the dawg for a matching array
        const size_t initial_hash = edges.hash();
        size_t hash = initial_hash;
        size_t inc = 9;

        do {
            if (hash_table[hash] == 0) {
                // This slot was free - add this set of edges to the DAWG
                hash_table[hash] = dawg.size();
                std::copy(edges.edges.cbegin(), edges.edges.cend(), std::back_inserter(dawg));
                return hash_table[hash] + 1;
            }
            else if (edges.equal(dawg.begin() + hash_table[hash])) {
                // This was a match!
                return hash_table[hash] + 1;
            }
            else {
                // Look for the next slot
                hash = hash_modulo_increment(hash, inc);
                inc = hash_modulo_increment(inc, 8);
            }
        } while (hash != initial_hash);

        throw std::logic_error("Hash table full");
    }

    void parse(std::istream& input) {
        WordBuffer word(input);
        std::vector<EdgeList> edges;
        edges.emplace_back();
        size_t idx = 0; // index of the last entry in 'edges'

        for (;;) {
            auto next = word.next();

            if (idx < next.first) {
                throw std::logic_error("common prefix length longer than previous word!");
            }

            // Unwind and commit the sets of edges back to the common point
            while (idx > next.first) {
                auto ready = std::move(edges.back());
                edges.pop_back();
                if (!ready.edges.empty()) {
                    ready.edges.back().setEndOfNode();
                    auto offset = insertEdges(ready);
                    edges.back().edges.back().setChildOffset(offset);
                }
                --idx;
            }

            if (next.second.empty()) {
                if (idx != 0) {
                    throw std::logic_error("End of input, but edges still pending");
                }
                break;
            }

            // Now we can add the new characters of the next word
            while (idx < next.second.size()) {
                bool last_in_word = idx + 1 == next.second.size();
                edges.back().edges.emplace_back(next.second[idx++], last_in_word);
                edges.emplace_back();
            }
        }

        // The final act is to mark the end of the root edge list, expand it to fill the 256
        // entries, mark the end of the root edge list (for compatibility with the file format)
        // and then insert it at the front of the dawg

        auto& root = edges.back().edges;
        if (!root.empty()) {
            root.back().setEndOfNode();
        }
        root.resize(MAX_CHARS);
        root.back().setEndOfNode();
        std::copy(root.cbegin(),root.cend(), dawg.begin());
    }

private:
    std::vector<Node> dawg;
    std::array<size_t, HASH_TABLE_SIZE> hash_table;
};

} // namespace Dawg

int main(int argc, char *argv[])
{
    Dawg::Dawg d;

    try {
        std::string command { (argc > 1) ? argv[1] : "" };
        std::string input   { (argc > 2) ? argv[2] : "" };
        std::string output  { (argc > 3) ? argv[3] : "" };

        if (command == "create") {
            if (input == "-") {
                d.parse(std::cin);
            }
            else {
                std::ifstream in(input, std::ios::in);
                d.parse(in);
            }
            d.save(std::ofstream(output, std::ios::out | std::ios::binary));
        }
        else if (command == "dump") {
            d.load(std::ifstream(input, std::ios::in | std::ios::binary));
            std::ofstream out(output, std::ios::out);
            d.dump(out ? out : std::cout);
        }
        else {
            std::cerr << "Unknown command (" << command << ").  Possible commands:\n\n"
                << "Syntax: zyzzyva-dawg create <input text file | '-'> <output DAWG file>\n"
                << "Syntax: zyzzyva-dawg dump <input DAWG file> [<output text file>]\n"
                << "\n";
        }

        return 0;
    }
    catch (std::exception const& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
