#include <iostream>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <cassert>
#include "de_serialization.h"
#include "magical_consts.h"
#include <vector>

#define OCTET_SIZE 8

bool operator<(const position& p1, const position& p2)
{
    return (p1.x < p2.x || (p1.x == p2.x && p1.y < p2.y));
}

bool operator<(const bomb& b1, const bomb& b2)
{
    return (b1.id < b2.id);
}

bool operator<(const bomb_placed_ev& b1, const bomb_placed_ev& b2)
{
    return (b1.bomb_id < b2.bomb_id);
}

bool operator<(const bomb_exploded_ev& b1, const bomb_exploded_ev& b2)
{
    return (b1.bomb_id < b2.bomb_id);
}

bool operator<(const player_moved_ev& p1, const player_moved_ev& p2)
{
    return (p1.player_id < p2.player_id);
}

bool operator<(const block_placed_ev& b1, const block_placed_ev& b2)
{
    return (b1.pos < b2.pos);
}

unsigned char* serialize(std::string str, unsigned char* ptr) {
    int8_t len = str.length();
    *ptr = len;             
    ptr++;
    char* ptr2 = (char*) ptr;
    strncpy(ptr2, str.c_str(), len);
    ptr += len;
    return ptr;
}

unsigned char* serialize(uint32_t num, uint8_t octets, unsigned char* ptr) {
    unsigned char byte;
    ptr += octets - 1;
    for (int i = 0; i < octets; i++) {
        byte = num & UINT8_MAX;
        num = num >> OCTET_SIZE;
        *ptr = byte;
        ptr--;
    }
    ptr += octets + 1;
    return ptr;
}

unsigned char* serialize(player p, unsigned char* ptr) {
    ptr = serialize(p.name, ptr);
    ptr = serialize(p.address, ptr);
    return ptr;
}

unsigned char* serialize(position p, unsigned char* ptr) {
    ptr = serialize(p.x, 2, ptr);
    ptr = serialize(p.y, 2, ptr);
    return ptr;
}

unsigned char* serialize(bomb b, unsigned char* ptr) {
    ptr = serialize(b.pos, ptr);
    ptr = serialize(b.timer, 2, ptr);
    return ptr;
}

unsigned char* serialize(std::map<uint8_t, player> players, unsigned char* ptr) {
    uint32_t map_len = players.size();
    ptr = serialize(map_len, 4, ptr);
    for (auto pl: players) {
        ptr = serialize(pl.first, 1, ptr);
        ptr = serialize(pl.second, ptr);
    }
    return ptr;
}

unsigned char* serialize(std::map<uint8_t, position> pl_pos, unsigned char* ptr) {
    uint32_t map_len = pl_pos.size();
    ptr = serialize(map_len, 4, ptr);
    for (auto pl: pl_pos) {
        ptr = serialize(pl.first, 1, ptr);
        ptr = serialize(pl.second, ptr);
    }
    return ptr;
}

unsigned char* serialize(std::map<uint8_t, uint32_t> scores, unsigned char* ptr) {
    uint32_t map_len = scores.size();
    ptr = serialize(map_len, 4, ptr);
    for (auto pl: scores) {
        ptr = serialize(pl.first, 1, ptr);
        ptr = serialize(pl.second, 4, ptr);
    }
    return ptr;
}

unsigned char* serialize(std::map<uint32_t, bomb> bombs, unsigned char* ptr) {
    uint32_t map_len = bombs.size();
    ptr = serialize(map_len, 4, ptr);
    for (auto b: bombs) {
        ptr = serialize(b.second, ptr);
    }
    return ptr;
}

unsigned char* serialize(std::set<position> pos_set, unsigned char* ptr) {
    uint32_t set_len = pos_set.size();
    ptr = serialize(set_len, 4, ptr);
    for (auto pos: pos_set) {
        ptr = serialize(pos, ptr);
    }
    return ptr;
}

unsigned char* serialize(std::set<bomb> bombs, unsigned char* ptr) {
    uint32_t set_len = bombs.size();
    ptr = serialize(set_len, 4, ptr);
    for (auto b: bombs) {
        ptr = serialize(b, ptr);
    }
    return ptr;
}

uint32_t serialize_lobby_message(unsigned char* ptr, game_state GAME_STATE) {
    unsigned char* ptr2 = ptr;
    *ptr = 0;
    ptr++;
    ptr = serialize(GAME_STATE.server_name, ptr);
    ptr = serialize(GAME_STATE.players_count, 1, ptr);
    ptr = serialize(GAME_STATE.size_x, 2, ptr);
    ptr = serialize(GAME_STATE.size_y, 2, ptr);
    ptr = serialize(GAME_STATE.game_length, 2, ptr);
    ptr = serialize(GAME_STATE.explosion_radius, 2, ptr);
    ptr = serialize(GAME_STATE.bomb_timer, 2, ptr);
    ptr = serialize(GAME_STATE.players, ptr);

    return ptr - ptr2;
}

uint32_t serialize_game_message(unsigned char* ptr, game_state GAME_STATE) {
    unsigned char* ptr2 = ptr;
    *ptr = 1;
    ptr++;
    ptr = serialize(GAME_STATE.server_name, ptr);
    ptr = serialize(GAME_STATE.size_x, 2, ptr);
    ptr = serialize(GAME_STATE.size_y, 2, ptr);
    ptr = serialize(GAME_STATE.game_length, 2, ptr);
    ptr = serialize(GAME_STATE.turn, 2, ptr);
    ptr = serialize(GAME_STATE.players, ptr);
    ptr = serialize(GAME_STATE.player_positions, ptr);
    ptr = serialize(GAME_STATE.blocks, ptr);
    ptr = serialize(GAME_STATE.bombs, ptr);
    ptr = serialize(GAME_STATE.explosions, ptr);
    ptr = serialize(GAME_STATE.scores, ptr);

    return ptr - ptr2;
}

uint32_t serialize_hello_message(unsigned char *ptr, game_state &GAME_STATE) {
    unsigned char* ptr2 = ptr;
    *ptr = HELLO;
    ptr++;
    ptr = serialize(GAME_STATE.server_name, ptr);
    ptr = serialize(GAME_STATE.players_count, 1, ptr);
    ptr = serialize(GAME_STATE.size_x, 2, ptr);
    ptr = serialize(GAME_STATE.size_y, 2, ptr);
    ptr = serialize(GAME_STATE.game_length, 2, ptr);
    ptr = serialize(GAME_STATE.explosion_radius, 2, ptr);
    ptr = serialize(GAME_STATE.bomb_timer, 2, ptr);

    return ptr - ptr2;
}

uint32_t serialize_accepted_player_message(unsigned char *ptr, player pl) {
    unsigned char* ptr2 = ptr;
    *ptr = ACCEPTED_PLAYER;
    ptr++;
    ptr = serialize(pl.id, 1, ptr);     //player_id
    ptr = serialize(pl, ptr);           //player

    return ptr - ptr2;
}


uint32_t serialize_game_started_message(unsigned char *ptr, game_state &GAME_STATE) {
    unsigned char* ptr2 = ptr;
    *ptr = GAME_STARTED;
    ptr++;
    ptr = serialize(GAME_STATE.players, ptr);

    return ptr - ptr2;
}

unsigned char* serialize(bomb_placed_ev &bp, unsigned char* ptr) {
    *ptr = BOMB_PLACED;
    ptr++;
    ptr = serialize(bp.bomb_id, 4, ptr);
    ptr = serialize(bp.pos, ptr);
    return ptr;
}

unsigned char* serialize(bomb_exploded_ev &be, unsigned char* ptr) {
    *ptr = BOMB_EXPLODED;
    ptr++;
    ptr = serialize(be.bomb_id, 4, ptr);
    ptr = serialize(be.robots_destroyed.size(), 4, ptr);//new
    for (auto rob: be.robots_destroyed) {
        ptr = serialize(rob, 1, ptr);
    }
    ptr = serialize(be.blocks_destroyed.size(), 4, ptr);//new
    for (auto pos: be.blocks_destroyed) {
        ptr = serialize(pos, ptr);
    }
    return ptr;
}

unsigned char* serialize(player_moved_ev &pm, unsigned char* ptr) {
    *ptr = PLAYER_MOVE;
    ptr++;
    ptr = serialize(pm.player_id, 1, ptr);
    ptr = serialize(pm.pos, ptr);
    return ptr;
}

unsigned char* serialize(block_placed_ev &bp, unsigned char* ptr) {
    *ptr = BLOCK_PLACED;
    ptr++;
    ptr = serialize(bp.pos, ptr);
    return ptr;
}

unsigned char* serialize(event &ev, unsigned char* ptr) {
    if (ev.type == BOMB_PLACED) {
        ptr = serialize(ev.bp, ptr);
    }
    else if (ev.type == BOMB_EXPLODED) {
        ptr = serialize(ev.be, ptr);
    }
    else if (ev.type == PLAYER_MOVE) {
        ptr = serialize(ev.pm, ptr);
    }
    else if (ev.type == BLOCK_PLACED) {
        ptr = serialize(ev.blp, ptr);
    }
    return ptr;
}

uint32_t serialize_turn_message(unsigned char *ptr, uint16_t turn, std::vector<event> &events) {
    unsigned char* ptr2 = ptr;
    *ptr = TURN;
    ptr++;
    ptr = serialize(turn, 2, ptr);     
    for (int i = 0; i < events.size(); i++){
        ptr = serialize(events[i], ptr);
    }

    return ptr - ptr2;
}


uint32_t serialize_turn_message(unsigned char *ptr, turn_msg turn) {
    unsigned char* ptr2 = ptr;
    *ptr = TURN;
    ptr++;
    ptr = serialize(turn.turn, 2, ptr);    
    uint32_t list_len = turn.pl_mov.size() + turn.block_pl.size() + turn.bomb_ex.size()
    + turn.bomb_pl.size();
    ptr = serialize(list_len, 4, ptr);

    for (auto ev : turn.pl_mov){
        ptr = serialize(ev, ptr);
    }
    for (auto ev : turn.bomb_ex){
        ptr = serialize(ev, ptr);
    }
    for (auto ev : turn.bomb_pl){
        ptr = serialize(ev, ptr);
    }
    for (auto ev : turn.block_pl){
        ptr = serialize(ev, ptr);
    }

    return ptr - ptr2;
}


uint32_t serialize_game_ended_message(unsigned char *ptr, game_state &GAME_STATE) {
        unsigned char* ptr2 = ptr;
    *ptr = GAME_ENDED;
    ptr++;
    ptr = serialize(GAME_STATE.scores, ptr);     
    return ptr - ptr2;
}

uint32_t deserialize_number(uint8_t octets, unsigned char* ptr) {
    uint32_t num = *ptr;
    for (int i = 1; i < octets; i++) {
        ptr++;
        num = num << OCTET_SIZE;
        num += *ptr;
    }
    return num;
}

std::string deserialize_string(unsigned char* ptr) {
    uint8_t len = *ptr;
    ptr++;
    char* ptr2 = (char*) ptr;
    std::string str(ptr2, len);
    return str;
}


position deserialize_position(unsigned char* ptr) {
    position pos;
    pos.x = deserialize_number(2, ptr);
    ptr += 2;
    pos.y = deserialize_number(2, ptr);
    return pos;
}


hello_msg deserialize_hello_message(unsigned char* ptr) {
    ptr++;
    hello_msg hm;
    hm.server_name = deserialize_string(ptr);
    ptr += *ptr + 1;
    hm.players_count = *ptr;
    ptr++;
    hm.size_x = deserialize_number(2, ptr);
    ptr += 2;
    hm.size_y = deserialize_number(2, ptr);
    ptr += 2;
    hm.game_length = deserialize_number(2, ptr);
    ptr += 2;
    hm.explosion_radius = deserialize_number(2, ptr);
    ptr += 2;
    hm.bomb_timer = deserialize_number(2, ptr);
    ptr += 2;
    return hm;
}


accepted_player_msg deserialize_accepted_player_message(unsigned char* ptr) {
   ptr++;
   accepted_player_msg ap;
   uint8_t id = *ptr;
   ptr++;
   std::string player_name = deserialize_string(ptr);
   ptr += *ptr + 1;
   std::string player_address = deserialize_string(ptr);
   player pl = {id, player_name, player_address};
   ap = {id, pl};
   return ap;
}

game_started_msg deserialize_game_started_message(unsigned char* ptr) {
    ptr++;
    game_started_msg gs;
    uint32_t len = deserialize_number(4, ptr);
    ptr += 4;
    uint8_t id;
    player pl;
    for (uint32_t i = 0; i < len; i++) {
        id = *ptr;
        ptr++;
        pl.name = deserialize_string(ptr);
        ptr += *ptr + 1;
        pl.address = deserialize_string(ptr);
        ptr += *ptr + 1;
        gs.players.insert({id, pl});
    }
    return gs;
}


std::pair<bomb_placed_ev, unsigned char*> deserialize_bomb_placed_event(unsigned char* ptr) {
    bomb b;
    b.id = deserialize_number(4, ptr);
    ptr += 4;
    b.pos = deserialize_position(ptr);
    ptr += 4;
    bomb_placed_ev bp = {b.id, b.pos};
    return {bp, ptr};
}

std::pair<bomb_exploded_ev, unsigned char*>  deserialize_bomb_exploded_event(unsigned char* ptr) {
    bomb_exploded_ev be;
    uint32_t bomb_id = deserialize_number(4, ptr);
    ptr += 4;
    be.bomb_id = bomb_id;

    uint32_t robots_count = deserialize_number(4, ptr);
    ptr += 4;
    uint8_t player_id;
    for (uint32_t i = 0; i < robots_count; i++) {
        player_id = deserialize_number(1, ptr);
        ptr++;
        be.robots_destroyed.insert(player_id);
    }

    uint32_t blocks_count = deserialize_number(4, ptr);
    ptr += 4;
    position pos;
    for (uint32_t i = 0; i < blocks_count; i++) {
        pos = deserialize_position(ptr);
        ptr += 4;
        be.blocks_destroyed.insert(pos);
    }
    return {be, ptr};
}

std::pair<player_moved_ev, unsigned char*> deserialize_player_moved_event(unsigned char* ptr) {
    uint8_t player_id = deserialize_number(1, ptr);
    ptr++;
    position pos;
    pos = deserialize_position(ptr);
    ptr += 4;
    player_moved_ev pm;
    pm = {player_id, pos};
    return {pm, ptr};
}

std::pair<block_placed_ev, unsigned char*>  deserialize_block_placed_event(unsigned char* ptr) {
    position pos;
    pos = deserialize_position(ptr);
    ptr += 4;
    block_placed_ev bp = {pos};
    return {bp, ptr};
}

turn_msg deserialize_turn_message(unsigned char* ptr) {
    ptr++;
    turn_msg tn;
    tn.turn = deserialize_number(2, ptr);
    ptr += 2;
    uint32_t list_len = deserialize_number(4, ptr);
    ptr += 4;
    uint8_t event_type;
    for (uint32_t i = 0; i < list_len; i++) {
        event_type = deserialize_number(1, ptr);
        ptr++;
        if (event_type == BOMB_PLACED) {
            auto ev_ptr = deserialize_bomb_placed_event(ptr);
            ptr = ev_ptr.second;
            auto _ev = ev_ptr.first;
            tn.bomb_pl.insert(_ev);
        }
        else if (event_type == BOMB_EXPLODED) {
            auto ev_ptr = deserialize_bomb_exploded_event(ptr);
            ptr = ev_ptr.second;
            auto _ev = ev_ptr.first;
            tn.bomb_ex.insert(_ev);
        }
        else if (event_type == PLAYER_MOVE) {
            auto ev_ptr = deserialize_player_moved_event(ptr);
            ptr = ev_ptr.second;
            auto _ev = ev_ptr.first;
            tn.pl_mov.insert(_ev);
        }
        else if (event_type == BLOCK_PLACED) {
            auto ev_ptr = deserialize_block_placed_event(ptr);
            ptr = ev_ptr.second;
            auto _ev = ev_ptr.first;
            tn.block_pl.insert(_ev);
        }
    }
    return tn;
}

game_ended_msg deserialize_game_ended_message(unsigned char* ptr) {
    ptr++;
    game_ended_msg ge;
    uint32_t map_len = deserialize_number(4, ptr);
    ptr += 4;

    uint8_t id;
    uint32_t score;
    for (uint32_t i = 0; i < map_len; i++) {
        id = *ptr;
        ptr++;
        score = deserialize_number(4, ptr);
        ptr += 4;
        ge.scores.insert({id, score});
    }
    return ge;
}
