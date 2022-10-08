#ifndef DE_SERIALIZATION_H
#define DE_SERIALIZATION_H

#include <iostream>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <cassert>
#include <vector>

struct position
{
    uint16_t x;
    uint16_t y;
};

struct player
{
    uint8_t id;
    std::string name;
    std::string address;
};

struct bomb
{
    uint32_t id;
    position pos;
    uint16_t timer;
};

struct game_state
{
    bool started = false;
    std::string server_name;
    uint8_t players_count;
    uint16_t size_x;
    uint16_t size_y;
    uint16_t game_length;
    uint16_t explosion_radius;
    uint16_t bomb_timer;
    uint16_t turn;
    std::map<uint8_t, player> players;
    std::map<uint8_t, position> player_positions;
    std::map<uint8_t, uint32_t> scores;
    std::map<uint32_t, bomb> bombs;
    std::set<position> blocks;
    std::set<bomb> bombs_vec;
    std::set<position> explosions;
};

bool operator<(const position &p1, const position &p2);

bool operator<(const bomb &b1, const bomb &b2);


struct bomb_placed_ev
{
    uint32_t bomb_id;
    position pos;
};

struct bomb_exploded_ev
{
    uint32_t bomb_id;
    std::set<uint8_t> robots_destroyed;
    std::set<position> blocks_destroyed;
};

struct player_moved_ev
{
    uint8_t player_id;
    position pos;
};

struct block_placed_ev
{
    position pos;
};

struct event {
    ~event() {}
    enum {BOMBPLACED, BOMBEXPLODED, PLAYERMOVED, BLOCKPLACED, EMPTY} type;
    union{
        bomb_placed_ev bp;
        bomb_exploded_ev be;
        player_moved_ev pm;
        block_placed_ev blp;
        int dummy;
    };
    event() {
        type = event::EMPTY;
        dummy = 0;
    }
};


unsigned char *serialize(std::string str, unsigned char *ptr);

unsigned char *serialize(uint32_t num, uint8_t octets, unsigned char *ptr);

unsigned char *serialize(player p, unsigned char *ptr);

unsigned char *serialize(position p, unsigned char *ptr);

unsigned char *serialize(bomb b, unsigned char *ptr);

unsigned char *serialize(std::map<uint8_t, player> players, unsigned char *ptr);

unsigned char *serialize(std::map<uint8_t, position> pl_pos, unsigned char *ptr);

unsigned char *serialize(std::map<uint8_t, uint32_t> scores, unsigned char *ptr);

unsigned char *serialize(std::map<uint32_t, bomb> bombs, unsigned char *ptr);

unsigned char *serialize(std::set<position> pos_set, unsigned char *ptr);

unsigned char *serialize(std::set<bomb> bombs, unsigned char *ptr);

unsigned char* serialize(bomb_exploded_ev &be, unsigned char* ptr);

uint32_t serialize_lobby_message(unsigned char *ptr, game_state GAME_STATE);

uint32_t serialize_game_message(unsigned char *ptr, game_state GAME_STATE);

uint32_t serialize_hello_message(unsigned char *ptr, game_state &GAME_STATE); 

uint32_t serialize_accepted_player_message(unsigned char *ptr, player pl);

uint32_t serialize_game_started_message(unsigned char *ptr, game_state &GAME_STATE);

unsigned char* serialize(bomb_placed_ev &bp, unsigned char* ptr);

unsigned char* serialize(bomb_exploded_ev &be, unsigned char* ptr);

unsigned char* serialize(player_moved_ev &pm, unsigned char* ptr);

unsigned char* serialize(block_placed_ev &bp, unsigned char* ptr);

unsigned char* serialize(event &ev, unsigned char* ptr);

uint32_t serialize_turn_message(unsigned char *ptr, uint16_t turn, std::vector<event> &events);

uint32_t serialize_game_ended_message(unsigned char *ptr, game_state &GAME_STATE);

uint32_t deserialize_number(uint8_t octets, unsigned char *ptr);

std::string deserialize_string(unsigned char *ptr);

position deserialize_position(unsigned char *ptr);

struct hello_msg
{
    std::string server_name;
    uint8_t players_count;
    uint16_t size_x;
    uint16_t size_y;
    uint16_t game_length;
    uint16_t explosion_radius;
    uint16_t bomb_timer;
};

struct accepted_player_msg
{
    uint8_t player_id;
    player pl;
};

struct game_started_msg
{
    std::map<uint8_t, player> players;
};


struct turn_msg
{
    uint16_t turn;
    std::set<bomb_placed_ev> bomb_pl;
    std::set<bomb_exploded_ev> bomb_ex;
    std::set<player_moved_ev> pl_mov;
    std::set<block_placed_ev> block_pl;
};

struct game_ended_msg
{
    std::map<uint8_t, uint32_t> scores;
};

bool operator<(const bomb_placed_ev &b1, const bomb_placed_ev &b2);

bool operator<(const bomb_exploded_ev &b1, const bomb_exploded_ev &b2);

bool operator<(const player_moved_ev &p1, const player_moved_ev &p2);

bool operator<(const block_placed_ev &b1, const block_placed_ev &b2);

uint32_t serialize_turn_message(unsigned char *ptr, turn_msg turn);

hello_msg deserialize_hello_message(unsigned char *ptr);

accepted_player_msg deserialize_accepted_player_message(unsigned char *ptr);

game_started_msg deserialize_game_started_message(unsigned char *ptr);

std::pair<bomb_placed_ev, unsigned char *> deserialize_bomb_placed_event(unsigned char *ptr);

std::pair<bomb_exploded_ev, unsigned char *> deserialize_bomb_exploded_event(unsigned char *ptr);

std::pair<player_moved_ev, unsigned char *> deserialize_player_moved_event(unsigned char *ptr);

std::pair<block_placed_ev, unsigned char *> deserialize_block_placed_event(unsigned char *ptr);

turn_msg deserialize_turn_message(unsigned char *ptr);

game_ended_msg deserialize_game_ended_message(unsigned char *ptr);

#endif /* DE_SERIALIZATION_H */