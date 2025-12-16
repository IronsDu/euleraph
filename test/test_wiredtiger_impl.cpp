#include <gtest/gtest.h>

#include "storage/writer_wiredtiger.hpp"
#include "storage/reader_wiredtiger.hpp"

class ut_wiredtiger_impl : public testing::Test
{
};

TEST_F(ut_wiredtiger_impl, normal)
{
    WriterWiredTiger::initialize_databse_schema();

    std::error_code ec;
    WT_CONNECTION*  conn;
    wiredtiger_open("graph_database", nullptr, "create", &conn);

    auto writer_wiredtiger = std::make_shared<WriterWiredTiger>(conn);

    const auto person_label_type_id_first  = writer_wiredtiger->write_label_type("Person");
    const auto person_label_type_id_second = writer_wiredtiger->write_label_type("Person");
    ASSERT_EQ(person_label_type_id_first, person_label_type_id_second);

    const auto dog_label_type_id_first  = writer_wiredtiger->write_label_type("Dog");
    const auto dog_label_type_id_second = writer_wiredtiger->write_label_type("Dog");
    ASSERT_EQ(dog_label_type_id_first, dog_label_type_id_second);

    const auto like_relation_type_id_first  = writer_wiredtiger->write_relation_type("Like");
    const auto like_relation_type_id_second = writer_wiredtiger->write_relation_type("Like");
    ASSERT_EQ(like_relation_type_id_first, like_relation_type_id_second);

    auto                                 reader_wiredtiger = std::make_shared<ReaderWiredTiger>(conn);
    auto                                 person_label_id   = reader_wiredtiger->get_label_type_id("Person");
    auto                                 dog_label_id      = reader_wiredtiger->get_label_type_id("Dog");
    std::vector<WriterInterface::Vertex> vertices;
    vertices.push_back({person_label_id.value(), "a"});
    vertices.push_back({dog_label_id.value(), "b"});

    ASSERT_EQ(person_label_type_id_first, person_label_id.value());
    ASSERT_EQ(dog_label_type_id_first, dog_label_id.value());

    writer_wiredtiger->write_vertices(vertices);

    auto a_vertex_id = reader_wiredtiger->get_vertex_id(person_label_id.value(), "a");
    auto b_vertex_id = reader_wiredtiger->get_vertex_id(dog_label_id.value(), "b");

    auto like_relation_id = reader_wiredtiger->get_relation_type_id("Like");
    ASSERT_EQ(like_relation_type_id_second, like_relation_id.value());

    std::vector<WriterInterface::Edge> edges;
    edges.push_back({like_relation_id.value(),
                     person_label_id.value(),
                     a_vertex_id.value(),
                     EdgeDirection::INCOMING,
                     dog_label_id.value(),
                     b_vertex_id.value()});
    edges.push_back({like_relation_id.value(),
                     person_label_id.value(),
                     a_vertex_id.value(),
                     EdgeDirection::INCOMING,
                     dog_label_id.value(),
                     3});
    edges.push_back({like_relation_id.value(),
                     person_label_id.value(),
                     a_vertex_id.value(),
                     EdgeDirection::INCOMING,
                     dog_label_id.value(),
                     4});

    edges.push_back(
        {like_relation_id.value(), person_label_id.value(), 5, EdgeDirection::INCOMING, dog_label_id.value(), 3});

    writer_wiredtiger->write_edges(edges);

    const auto neighbors_edges = reader_wiredtiger->get_neighbors_by_start_vertex(a_vertex_id.value(),
                                                                                  person_label_id.value(),
                                                                                  EdgeDirection::INCOMING,
                                                                                  like_relation_id.value());
    for (const auto& neighbor : neighbors_edges)
    {
        ASSERT_EQ(neighbor.start_vertex_id, a_vertex_id.value());
        ASSERT_EQ(neighbor.start_label_type_id, person_label_id.value());
        ASSERT_EQ(neighbor.relation_type_id, like_relation_id.value());
        ASSERT_EQ(neighbor.direction, EdgeDirection::INCOMING);
        ASSERT_EQ(neighbor.end_label_type_id, person_label_id.value());
        ASSERT_TRUE(neighbor.end_vertex_id == b_vertex_id.value() || neighbor.end_vertex_id == 2 ||
                    neighbor.end_vertex_id == 3);
    }
    ASSERT_EQ(neighbors_edges.size(), 3);
}