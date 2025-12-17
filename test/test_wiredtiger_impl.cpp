#include <gtest/gtest.h>

#include "storage/wiredtiger_common.hpp"
#include "storage/writer_wiredtiger.hpp"
#include "storage/reader_wiredtiger.hpp"

class ut_wiredtiger_impl : public testing::Test
{
};

TEST_F(ut_wiredtiger_impl, normal)
{
    const std::string PersonLabelTypeName  = "Person";
    const std::string DogLabelTypeName     = "Dog";
    const std::string LikeRelationTypeName = "Like";

    const std::string db_name = "db_test";
    wiredtiger_initialize_databse_schema(db_name);

    std::error_code ec;
    WT_CONNECTION*  conn;
    wiredtiger_open(db_name.c_str(), nullptr, "create", &conn);

    auto writer_wiredtiger = std::make_shared<WriterWiredTiger>(conn);

    const auto person_label_type_id_first  = writer_wiredtiger->write_label_type(PersonLabelTypeName);
    const auto person_label_type_id_second = writer_wiredtiger->write_label_type(PersonLabelTypeName);
    ASSERT_EQ(person_label_type_id_first, person_label_type_id_second);

    const auto dog_label_type_id_first  = writer_wiredtiger->write_label_type(DogLabelTypeName);
    const auto dog_label_type_id_second = writer_wiredtiger->write_label_type(DogLabelTypeName);
    ASSERT_EQ(dog_label_type_id_first, dog_label_type_id_second);

    const auto like_relation_type_id_first  = writer_wiredtiger->write_relation_type(LikeRelationTypeName);
    const auto like_relation_type_id_second = writer_wiredtiger->write_relation_type(LikeRelationTypeName);
    ASSERT_EQ(like_relation_type_id_first, like_relation_type_id_second);

    auto                                 reader_wiredtiger = std::make_shared<ReaderWiredTiger>(conn);
    auto                                 person_label_id   = reader_wiredtiger->get_label_type_id(PersonLabelTypeName);
    auto                                 dog_label_id      = reader_wiredtiger->get_label_type_id(DogLabelTypeName);
    std::vector<WriterInterface::Vertex> vertices;
    vertices.push_back({person_label_id.value(), "a"});
    vertices.push_back({dog_label_id.value(), "b"});

    ASSERT_EQ(person_label_type_id_first, person_label_id.value());
    ASSERT_EQ(dog_label_type_id_first, dog_label_id.value());

    writer_wiredtiger->write_vertices(vertices);

    auto a_vertex_id = reader_wiredtiger->get_vertex_id(person_label_id.value(), "a");
    auto b_vertex_id = reader_wiredtiger->get_vertex_id(dog_label_id.value(), "b");

    auto like_relation_id = reader_wiredtiger->get_relation_type_id(LikeRelationTypeName);
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
    ASSERT_EQ(neighbors_edges.size(), 3);
    for (const auto& neighbor : neighbors_edges)
    {
        ASSERT_EQ(neighbor.start_vertex_id, a_vertex_id.value());
        ASSERT_EQ(neighbor.start_label_type_id, person_label_id.value());
        ASSERT_EQ(neighbor.relation_type_id, like_relation_id.value());
        ASSERT_EQ(neighbor.direction, EdgeDirection::INCOMING);
        ASSERT_EQ(neighbor.end_label_type_id, dog_label_id.value());
        std::cout << neighbor.end_vertex_id << std::endl;
        ASSERT_TRUE(neighbor.end_vertex_id == b_vertex_id.value() || neighbor.end_vertex_id == 2 ||
                    neighbor.end_vertex_id == 3 || neighbor.end_vertex_id == 4);
    }

    auto person_type_name = reader_wiredtiger->get_label_type_by_id(person_label_id.value());
    ASSERT_EQ(person_type_name.value(), PersonLabelTypeName);
    auto dog_type_name = reader_wiredtiger->get_label_type_by_id(dog_label_id.value());
    ASSERT_EQ(dog_type_name.value(), DogLabelTypeName);

    auto like_relation_type_name = reader_wiredtiger->get_relation_type_by_id(like_relation_id.value());
    ASSERT_EQ(like_relation_type_name.value(), LikeRelationTypeName);

    auto a_vertex_pk = reader_wiredtiger->get_vertex_pk_by_id(a_vertex_id.value());
    ASSERT_EQ(a_vertex_pk.value(), "a");
    auto b_vertex_pk = reader_wiredtiger->get_vertex_pk_by_id(b_vertex_id.value());
    ASSERT_EQ(b_vertex_pk.value(), "b");
}