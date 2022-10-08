#include <iostream>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <set>
#include <cassert>
#include <boost/program_options.hpp>
#include <unordered_set>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include "de_serialization.h"
#include "magical_consts.h"
#include <thread>
#include <mutex>
#include <memory>
#include <chrono>
#include <shared_mutex>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
namespace po = boost::program_options;
using boost::asio::ip::tcp;
using boost::asio::ip::udp;
typedef std::shared_ptr<tcp::socket> socket_ptr;

typedef boost::shared_mutex Lock;
Lock data_lock;
Lock server_lock; 

#define BUFFER_LENGTH 65507
unsigned char temp_buff[BUFFER_LENGTH];
std::vector<std::thread> threads;

struct server_pamrameters
{
    uint16_t bomb_timer;
    uint16_t players_count;
    uint64_t turn_duration;
    uint16_t explosion_radius;
    uint16_t initial_blocks;
    uint16_t game_length;
    std::string server_name;
    uint16_t port;
    uint32_t seed;
    uint16_t size_x;
    uint16_t size_y;
    uint32_t bomb_id = 0;
};

server_pamrameters PARAMS;
game_state GAME_STATE;

struct client
{
    uint8_t connection_id;
    uint8_t player_id; // 0 if not a player
    bool connected;
    socket_ptr socket;
    bool finished; // true if thread associated with connection finished so it can be forgotten
};

bool operator<(const client &c1, const client &c2)
{
    return (c1.connection_id < c2.connection_id);
}

struct server_state
{
    server_state() : players_events(CLIENTS_LIMIT + 1) {}
    std::map<uint8_t, client> clients_connections;
    std::vector<event> players_events;
    uint16_t joined_players = 0;
    std::vector<turn_msg> turns;
};

server_state SERVER_STATE;

void parse_input(int argc, char **argv)
{
    po::options_description desc("Program usage");
    po::variables_map vm;
    desc.add_options()("help,h", "produce help message")("bomb-timer,b", po::value<uint16_t>(), "set bomb timer")
    ("players-count,c", po::value<uint16_t>(), "set players count")
    ("turn-duration,d", po::value<uint64_t>(), "set turn duration")
    ("explosion-radius,e", po::value<uint16_t>(), "set explosion radius")
    ("initial-blocks,k", po::value<uint16_t>(), "set initial blocks")
    ("game-length,l", po::value<uint16_t>(), "set game length")
    ("port,p", po::value<uint16_t>(), "set port")("seed,s", po::value<uint32_t>(), "set seed (optional)")
    ("size-x,x", po::value<uint16_t>(), "set size x")("size-y,y", po::value<uint16_t>(), "set size y")
    ("server-name,n", po::value<std::string>(), "set server name");

    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << "\n";
        exit(1);
    }

    if (!vm.count("bomb-timer") || !vm.count("players-count") ||
        !vm.count("turn-duration") || !vm.count("explosion-radius") ||
        !vm.count("initial-blocks") || !vm.count("game-length") ||
        !vm.count("port") || !vm.count("size-x") ||
        !vm.count("size-y") || !vm.count("server-name"))
    {
        std::cerr << "Run the program with --help flag or provide all the other \
        parameters not listed as optional:\n"
                  << desc << "\n";
        exit(1);
    }

    GAME_STATE.server_name = vm["server-name"].as<std::string>();
    GAME_STATE.bomb_timer = vm["bomb-timer"].as<uint16_t>();
    GAME_STATE.players_count = vm["players-count"].as<uint16_t>();
    GAME_STATE.players_count = (uint8_t)GAME_STATE.players_count;
    PARAMS.server_name = vm["server-name"].as<std::string>();
    PARAMS.bomb_timer = vm["bomb-timer"].as<uint16_t>();
    PARAMS.players_count = vm["players-count"].as<uint16_t>();
    PARAMS.players_count = PARAMS.players_count;
    PARAMS.turn_duration = vm["turn-duration"].as<uint64_t>();
    GAME_STATE.explosion_radius = vm["explosion-radius"].as<uint16_t>();
    PARAMS.explosion_radius = vm["explosion-radius"].as<uint16_t>();
    PARAMS.initial_blocks = vm["initial-blocks"].as<uint16_t>();
    GAME_STATE.game_length = vm["game-length"].as<uint16_t>();
    PARAMS.game_length = vm["game-length"].as<uint16_t>();
    PARAMS.port = vm["port"].as<uint16_t>();
    GAME_STATE.size_x = vm["size-x"].as<uint16_t>();
    GAME_STATE.size_y = vm["size-y"].as<uint16_t>();
    PARAMS.size_x = vm["size-x"].as<uint16_t>();
    PARAMS.size_y = vm["size-y"].as<uint16_t>();

    if (!vm.count("seed"))
        PARAMS.seed = std::chrono::system_clock::now().time_since_epoch().count();
    else
        PARAMS.seed = vm["seed"].as<uint32_t>();
}

uint32_t next_random()
{
    PARAMS.seed = ((uint64_t)PARAMS.seed * 48271) % 2147483647;
    return PARAMS.seed;
}

uint8_t first_free_connection_id()
{
    uint8_t free_id = CLIENTS_LIMIT + 1;
    boost::shared_lock<Lock> slock(server_lock);
    for (uint8_t i = 1; i <= CLIENTS_LIMIT; i++)
    {
        if (!SERVER_STATE.clients_connections.count(i))
        {
            free_id = i;
            break;
        }
    }
    assert(free_id <= CLIENTS_LIMIT);
    return free_id;
}

uint8_t get_new_player_id()         // inherits data lock
{
    uint8_t id = GAME_STATE.players.size() + 1;
    return id;
}

uint16_t connections_count()
{
    boost::shared_lock<Lock> slock(server_lock);
    uint16_t count = SERVER_STATE.clients_connections.size();
    return count;
}

void disconnect_during_game(uint8_t conn_id);

unsigned char *read_exact_number_of_bytes(tcp::socket &tcp_socket, unsigned char *ptr, size_t n, uint8_t conn_id)
{
    boost::system::error_code err;
    size_t read_bytes = boost::asio::read(tcp_socket, boost::asio::buffer(temp_buff),
                                          boost::asio::transfer_exactly(n), err);
    if (err)
    {   
        std::cerr << "error while receiving message from client " << conn_id << "; client will be disconnected\n";
        disconnect_during_game(conn_id);
    }
    assert(read_bytes == n);
    for (uint8_t i = 0; i < n; i++)
    {
        *ptr = temp_buff[i];
        ptr++;
    }
    return ptr;
}

unsigned char *read_exact_string(tcp::socket &tcp_socket, unsigned char *ptr, uint8_t conn_id)
{
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 1, conn_id); // string length
    uint8_t len = temp_buff[0];
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, len, conn_id);
    return ptr;
}

uint32_t get_bomb_id()
{
    return PARAMS.bomb_id++; // inherits lock
}

bool correct_position(position pos)
{
    return (pos.x >= 0 && pos.x < PARAMS.size_x && pos.y >= 0 && pos.y < PARAMS.size_y);
}

position new_position(position curr_pos, uint8_t dir)
{
    position dest = curr_pos;
    switch (dir)
    {
    case UP:
        dest.y++;
        break;
    case DOWN:
        dest.y--;
        break;
    case RIGHT:
        dest.x++;
        break;
    case LEFT:
        dest.x--;
        break;
    default:
        break;
    }
    return dest;
}

std::string get_address(socket_ptr socket)
{
    std::stringstream ss;
    ss << (*socket.get()).remote_endpoint();
    return ss.str();
}

bool game_started()
{
    boost::shared_lock<Lock> lock(data_lock);
    return GAME_STATE.started;
}

uint16_t joined_players()
{
    boost::shared_lock<Lock> slock(server_lock);
    return SERVER_STATE.joined_players;
}

//inherits lock
bomb_exploded_ev create_bomb_exploded_ev(uint32_t id, std::set<uint8_t> &killed_players,
                                         std::set<position> &destroyed_blocks)
{
    bomb_exploded_ev be;
    be.bomb_id = id;

    position pos = GAME_STATE.bombs[id].pos;
    uint16_t down_reach = pos.y - GAME_STATE.explosion_radius;
    uint16_t up_reach = pos.y + GAME_STATE.explosion_radius;
    uint16_t left_reach = pos.x - GAME_STATE.explosion_radius;
    uint16_t right_reach = pos.x + GAME_STATE.explosion_radius;

    for (uint16_t i = 0; i <= GAME_STATE.explosion_radius; i++)
    {
        uint16_t px = pos.x + i;
        if (GAME_STATE.blocks.count({px, pos.y}))
        { // block
            be.blocks_destroyed.insert({px, pos.y});
            destroyed_blocks.insert({px, pos.y});
            right_reach = px;
            break;
        }
        if (px == GAME_STATE.size_x - 1) // edge of the board
            break;
    }
    for (uint16_t i = 1; i <= GAME_STATE.explosion_radius; i++)
    {
        uint16_t px = pos.x - i;
        if (GAME_STATE.blocks.count({px, pos.y}))
        { // block
            be.blocks_destroyed.insert({px, pos.y});
            destroyed_blocks.insert({px, pos.y});
            left_reach = px;
            break;
        }
        if (px == 0) // edge of the board
            break;
    }
    for (uint16_t i = 1; i <= GAME_STATE.explosion_radius; i++)
    {
        uint16_t py = pos.y + i;
        if (GAME_STATE.blocks.count({pos.x, py}))
        { // block
            be.blocks_destroyed.insert({pos.x, py});
            destroyed_blocks.insert({pos.x, py});
            up_reach = py;
            break;
        }
        if (py == GAME_STATE.size_y - 1) // edge of the board
            break;
    }
    for (uint16_t i = 1; i <= GAME_STATE.explosion_radius; i++)
    {
        uint16_t py = pos.y - i;
        if (GAME_STATE.blocks.count({pos.x, py}))
        { // block
            be.blocks_destroyed.insert({pos.x, py});
            destroyed_blocks.insert({pos.x, py});
            down_reach = py;
            break;
        }
        if (py == 0) // edge of the board
            break;
    }
    for (auto el : GAME_STATE.player_positions)
    {
        position p = el.second;
        if (p.x == pos.x && p.y >= down_reach && p.y <= up_reach)
        {
            be.robots_destroyed.insert(el.first);
            killed_players.insert(el.first);
        }
        if (p.y == pos.y && p.x >= left_reach && p.x <= right_reach)
        {
            be.robots_destroyed.insert(el.first);
            killed_players.insert(el.first);
        }
    }
    return be;
}

position get_random_position()
{
    uint16_t px = next_random() % PARAMS.size_x;
    uint16_t py = next_random() % PARAMS.size_y;
    return {px, py};
}

turn_msg prepare_turn_0()
{
    turn_msg t;
    t.turn = 0;

    boost::lock_guard<Lock> lock(data_lock);
    for (auto pl : GAME_STATE.players)
    {
        position pos = get_random_position();
        t.pl_mov.insert({pl.first, pos});
        GAME_STATE.player_positions.insert({pl.first, pos});
    }
    for (auto i = 0; i < PARAMS.initial_blocks; i++)
    {
        position pos = get_random_position();
        t.block_pl.insert({pos});
        GAME_STATE.blocks.insert(pos);
    }

    return t;
}

void reset_params()             //inherits exclusive data_lock and server_lock
{
    PARAMS.bomb_id = 0;
    GAME_STATE.blocks.clear();
    GAME_STATE.bombs.clear();
    GAME_STATE.player_positions.clear();
    GAME_STATE.players.clear();
    GAME_STATE.scores.clear();
    GAME_STATE.started = false;
    GAME_STATE.turn = 0;
    SERVER_STATE.joined_players = 0;
    SERVER_STATE.turns.clear();
    for (auto &conn : SERVER_STATE.clients_connections)
    {
        conn.second.player_id = 0;
    }
    for (auto &ev : SERVER_STATE.players_events)
    {
        ev.type = event::EMPTY;
    }
}

void disconnect(uint8_t conn_id) // inherits exclusive server lock
{
    boost::system::error_code ec;
    auto sock = SERVER_STATE.clients_connections[conn_id].socket;
    sock.get()->shutdown(tcp::socket::shutdown_both, ec);
    sock.get()->close();
    SERVER_STATE.clients_connections.erase(conn_id);
}

void disconnect_during_game(uint8_t conn_id)
{
    boost::lock_guard<Lock> slock(server_lock);
    SERVER_STATE.clients_connections[conn_id].connected = false;
}


void handle_join(socket_ptr socket, std::string name, uint8_t conn_id)
{
    unsigned char buff[BUFFER_LENGTH];
    uint32_t mess_len;
    boost::system::error_code ignored_error;
    uint8_t player_id;
    player pl;
    {
        {
            boost::lock_guard<Lock> lock(data_lock);
            {
                boost::shared_lock<Lock> slock(server_lock);
                if (GAME_STATE.players.size() >= PARAMS.players_count ||
                // return if already joined
                    SERVER_STATE.clients_connections[conn_id].player_id) {
                    return;
                }
            }
            std::string address = get_address(socket);
            player_id = get_new_player_id();
            pl = {player_id, name, address};

            GAME_STATE.players.insert({player_id, pl});
            GAME_STATE.scores.insert({player_id, 0});
        }

        mess_len = serialize_accepted_player_message(buff, pl);
        {
            boost::shared_lock<Lock> slock(server_lock);
            for (auto &conn : SERVER_STATE.clients_connections)
            {
                boost::asio::write((*conn.second.socket.get()), boost::asio::buffer(buff, mess_len), ignored_error);
            }
        }
        {
            boost::lock_guard<Lock> slock(server_lock);
            SERVER_STATE.clients_connections[conn_id].player_id = player_id; 
            SERVER_STATE.joined_players++;
        }
    }
}

void get_one_message(socket_ptr socket, boost::system::error_code &err,
                     boost::array<char, 8> &buf, uint8_t conn_id)
{
    size_t read_bytes = boost::asio::read((*socket.get()), boost::asio::buffer(buf),
                                          boost::asio::transfer_exactly(1), err);

    if (err || buf[0] > MOVE)
    {
        std::cerr << "error while receiving message from client " << conn_id << "; client will be disconnected\n";
        disconnect_during_game(conn_id);
        return;
    }

    if (buf[0] == MOVE)
    {
        size_t read_bytes = boost::asio::read((*socket.get()), boost::asio::buffer(buf),
                                              boost::asio::transfer_exactly(1), err);
        if (err || buf[0] > LEFT)
        {
            std::cerr << "error while receiving message from client " << conn_id << "; client will be disconnected\n";
            disconnect_during_game(conn_id);
            return;
        }
    }
    else if (buf[0] == JOIN)
    {
        unsigned char name_buf[MAX_STRING_LEN];
        unsigned char *ptr = name_buf;
        ptr = read_exact_string((*socket.get()), ptr, conn_id);
        std::string name = deserialize_string(name_buf);
        handle_join(socket, name, conn_id);
        return;
    }
}

void receive_client_messages(uint8_t conn_id)
{
    socket_ptr socket;
    boost::system::error_code err;
    boost::array<char, 8> buf;
    uint8_t player_id;
    position curr_pos;

    {
        boost::shared_lock<Lock> slock(server_lock);
        socket = SERVER_STATE.clients_connections[conn_id].socket;
        player_id = SERVER_STATE.clients_connections[conn_id].player_id;
        if (player_id) {
            boost::shared_lock<Lock> lock(data_lock);
            curr_pos = GAME_STATE.player_positions[player_id];
        }
    }

    bool connected = true;

    while (connected)
    {
        {
            boost::shared_lock<Lock> slock(server_lock);
            if (!SERVER_STATE.clients_connections[conn_id].player_id)
            {
                //client woke up after the game ended and is not a player anymore
                break;
            }
        }

        size_t read_bytes = boost::asio::read((*socket.get()), boost::asio::buffer(buf),
                                              boost::asio::transfer_exactly(1), err);
        
        if (err || buf[0] > MOVE)
        {
            std::cerr << "error while receiving message from client " << (uint16_t)conn_id << "; client will be disconnected\n";
            {
                boost::shared_lock<Lock> slock(server_lock);
                SERVER_STATE.players_events[player_id].type = event::EMPTY;
                SERVER_STATE.players_events[player_id].dummy = 0;
            }
            disconnect_during_game(conn_id);
            connected = false;
            break;
        }
        if (buf[0] == JOIN)
        {
            unsigned char string_buff[MAX_STRING_LEN];
            read_exact_string((*socket.get()), string_buff, conn_id);
            {
                boost::shared_lock<Lock> slock(server_lock);
                // game finished when client was waiting for a message - it can possibly join again
                if (SERVER_STATE.clients_connections[conn_id].player_id)
                {
                    continue;
                }
            }
            std::string name = deserialize_string(string_buff);
            handle_join(socket, name, conn_id);
        }
        if (buf[0] == MOVE)
        {
            size_t read_bytes = boost::asio::read((*socket.get()), boost::asio::buffer(buf),
                                                  boost::asio::transfer_exactly(1), err);
            if (err || buf[0] > LEFT)
            {
                std::cerr << "error while receiving message from client " << conn_id << "; client will be disconnected\n";
                {
                    boost::shared_lock<Lock> slock(server_lock);
                    SERVER_STATE.players_events[player_id].type = event::EMPTY;
                    SERVER_STATE.players_events[player_id].dummy = 0;
                }
                disconnect_during_game(conn_id);
                connected = false;
                break;
            }

            boost::shared_lock<Lock> lock(data_lock);
            curr_pos = GAME_STATE.player_positions[player_id];
            position new_pos = new_position(curr_pos, buf[0]);
            boost::shared_lock<Lock> slock(server_lock);
            if (!correct_position(new_pos) || GAME_STATE.blocks.count(new_pos))
            {
                SERVER_STATE.players_events[player_id].type = event::EMPTY;
                SERVER_STATE.players_events[player_id].dummy = 0;
                continue;
            }
            player_moved_ev pm = {player_id, new_pos};
            SERVER_STATE.players_events[player_id].type = event::PLAYERMOVED;
            SERVER_STATE.players_events[player_id].pm = pm;
        }
        else if (buf[0] == PLACE_BOMB)
        {
            boost::shared_lock<Lock> lock(data_lock);
            curr_pos = GAME_STATE.player_positions[player_id];
            bomb_placed_ev bp = {get_bomb_id(), curr_pos};
            boost::shared_lock<Lock> slock(server_lock);
            SERVER_STATE.players_events[player_id].type = event::BOMBPLACED;
            SERVER_STATE.players_events[player_id].bp = bp;
        }
        else if (buf[0] == PLACE_BLOCK)
        {
            boost::shared_lock<Lock> lock(data_lock);
            curr_pos = GAME_STATE.player_positions[player_id];
            bool blocked = GAME_STATE.blocks.count(curr_pos);
            boost::shared_lock<Lock> slock(server_lock);
            if (blocked)
            {
                SERVER_STATE.players_events[player_id].type = event::EMPTY;
                SERVER_STATE.players_events[player_id].dummy = 0;
                continue;
            }
            block_placed_ev bp = {curr_pos};
            SERVER_STATE.players_events[player_id].type = event::BLOCKPLACED;
            SERVER_STATE.players_events[player_id].blp = bp;
        }
    }
}

void handle_non_player(uint8_t conn_id)
{
    boost::system::error_code err;
    boost::array<char, 8> buf;
    uint8_t player_id = 0;
    socket_ptr socket;
    bool connected;
    {
        boost::shared_lock<Lock> slock(server_lock);
        socket = SERVER_STATE.clients_connections[conn_id].socket;
        connected = SERVER_STATE.clients_connections[conn_id].connected;
    }

    while (connected)
    {
        get_one_message(socket, err, buf, conn_id);
        {
            boost::shared_lock<Lock> slock(server_lock);
            connected = SERVER_STATE.clients_connections[conn_id].connected;
            player_id = SERVER_STATE.clients_connections[conn_id].player_id;
        }
        if (!connected)
            break;
        if (!player_id)
            continue; // client didn't join the game - they remain a watcher
        // client joined and will take part in the game - leaving loop and function
        break;
    }
}

// handles connection to one client:
// receiving join message and sending answer,
// receiving messages with client moves if client is an active player
// receiving and ignoring correct messages if client in lobby or is a watcher
// closes connection and terminates if incorrect message received
void handle_connection(uint8_t conn_id)
{
    while (true)
    {
        handle_non_player(conn_id);
        // here if client joined or disconnected
        {
            boost::upgrade_lock<Lock> slock(server_lock);
            if (!SERVER_STATE.clients_connections[conn_id].connected)
            {
                boost::upgrade_to_unique_lock<Lock> uniqueLock(slock);
                SERVER_STATE.clients_connections[conn_id].finished = true;
                return;
            }
        }
        // client is a player waiting for game start
        while (!game_started())
        {
            ;;
        }
        receive_client_messages(conn_id); 
    }
}

void handle_turn()
{
    boost::lock_guard<Lock> lock(data_lock);
    unsigned char buff[BUFFER_LENGTH];
    unsigned char *ptr = buff;
    turn_msg turn;
    turn.turn = GAME_STATE.turn;
    GAME_STATE.turn++;

    std::set<uint8_t> killed_players;
    std::set<position> destroyed_blocks;
    for (auto &b : GAME_STATE.bombs)
    {
        b.second.timer--;
        if (b.second.timer == 0)
        {
            auto be = create_bomb_exploded_ev(b.first, killed_players, destroyed_blocks);
            turn.bomb_ex.insert(be);
        }
    }
    for (auto pl : killed_players)
    {
        auto pos = get_random_position();
        turn.pl_mov.insert({pl, pos});
        GAME_STATE.player_positions[pl] = pos;
        GAME_STATE.scores[pl]++;
    }

    boost::upgrade_lock<Lock> slock(server_lock);

    for (auto i = 0; i < SERVER_STATE.players_events.size(); i++)
    { 
        auto &ev = SERVER_STATE.players_events[i];
        if (killed_players.count(i))
            continue;
        if (ev.type == event::PLAYERMOVED)
        {
            turn.pl_mov.insert(ev.pm);
            GAME_STATE.player_positions[ev.pm.player_id] = ev.pm.pos; 
        }
        else if (ev.type == event::BOMBPLACED)
        {
            turn.bomb_pl.insert(ev.bp);
            bomb b = {ev.bp.bomb_id, ev.bp.pos, PARAMS.bomb_timer};
            GAME_STATE.bombs.insert({ev.bp.bomb_id, b});
        }
        else if (ev.type == event::BLOCKPLACED)
        {
            turn.block_pl.insert(ev.blp);
            GAME_STATE.blocks.insert(ev.blp.pos);
        }
    }

    boost::system::error_code ignored_error;
    auto mess_len = serialize_turn_message(buff, turn);
    for (auto conn : SERVER_STATE.clients_connections)
    {
        auto socket = conn.second.socket;
        boost::asio::write((*socket.get()), boost::asio::buffer(buff, mess_len), ignored_error);
    }

    for (auto bl : destroyed_blocks)
    {
        GAME_STATE.blocks.erase(bl); 
    }

    boost::upgrade_to_unique_lock<Lock> uniqueLock(slock);

    for (auto &ev : SERVER_STATE.players_events)
    {
        ev.type = event::EMPTY;
        ev.dummy = 0;
    }

    SERVER_STATE.turns.push_back(turn);
}

// handles game start and turn 0
void handle_game_start()
{
    while (joined_players() < PARAMS.players_count)
    {
        ;;
    }

    unsigned char buff[BUFFER_LENGTH];
    boost::system::error_code ignored_error;
    {
        boost::shared_lock<Lock> lock(data_lock);
        auto mess_len = serialize_game_started_message(buff, GAME_STATE);

        boost::shared_lock<Lock> slock(server_lock);
        for (auto conn : SERVER_STATE.clients_connections)
        {
            auto socket = conn.second.socket;
            boost::asio::write((*socket.get()), boost::asio::buffer(buff, mess_len), ignored_error);
        }
    }
    auto turn_0 = prepare_turn_0();
    auto mess_len = serialize_turn_message(buff, turn_0);

    {
        boost::shared_lock<Lock> slock(server_lock);
        for (auto conn : SERVER_STATE.clients_connections)
        {
            auto socket = conn.second.socket;
            boost::asio::write((*socket.get()), boost::asio::buffer(buff, mess_len), ignored_error);
        }
    }

    {
        boost::lock_guard<Lock> lock(data_lock);
        {
            boost::lock_guard<Lock> slock(server_lock);
            SERVER_STATE.turns.push_back(turn_0);
        }
        GAME_STATE.turn++;
        GAME_STATE.started = true;
    }
}

void handle_game_end()
{
    unsigned char buff[BUFFER_LENGTH];
    uint32_t mess_len;
    {
        boost::lock_guard<Lock> slock(server_lock); //TODO: upgrading?

        {
            boost::shared_lock<Lock> lock(data_lock);
            mess_len = serialize_game_ended_message(buff, GAME_STATE);
        }

        boost::system::error_code ignored_error;

        for (auto &conn : SERVER_STATE.clients_connections)
        {
            if (!conn.second.connected && conn.second.finished)
            {
                disconnect(conn.second.connection_id);
            }
        }
        
        //sending game end
        for (auto &conn : SERVER_STATE.clients_connections)
        {
            auto socket = conn.second.socket;
            boost::asio::write((*socket.get()), boost::asio::buffer(buff, mess_len), ignored_error);
        }
        {
            boost::lock_guard<Lock> lock(data_lock);
            reset_params();
        }
    }
}

// processes and sends game start, turns and game end for all players
void handle_coordination()
{
    while (true)
    {
        handle_game_start();
        for (auto i = 1; i <= PARAMS.game_length; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(PARAMS.turn_duration));
            handle_turn();
        }
        handle_game_end();
    }
}

// waits for new connections
void accept_connections()
{
    try
    {
        boost::asio::io_context io_context;
        tcp::endpoint server_endpoint{tcp::v6(), PARAMS.port};
        tcp::acceptor acceptor{io_context, server_endpoint};
        unsigned char hello_buffer[356];
        auto mess_len = serialize_hello_message(hello_buffer, GAME_STATE);
        boost::system::error_code ignored_error;

        for (;;)
        {   
            // accepting new connections if the limit is not reached
            if (connections_count() < CLIENTS_LIMIT)
            {
                auto sock_ptr = std::make_shared<tcp::socket>(io_context);
                acceptor.accept(*sock_ptr.get());
                (*sock_ptr.get()).set_option(tcp::no_delay(true));

                boost::asio::write((*sock_ptr.get()), boost::asio::buffer(hello_buffer, mess_len), ignored_error);

                uint8_t conn_id = first_free_connection_id();
                {
                    boost::lock_guard<Lock> slock(server_lock);
                    if (SERVER_STATE.clients_connections.size() >= CLIENTS_LIMIT)
                        continue;
                    SERVER_STATE.clients_connections.insert({conn_id, {conn_id, 0, true, sock_ptr, false}});
                }

                unsigned char buff[BUFFER_LENGTH];
                
                {
                    boost::shared_lock<Lock> lock(data_lock);
                    boost::shared_lock<Lock> slock(server_lock);

                    if (GAME_STATE.started)
                    {
                        // sending game started and turns
                        uint32_t mess_len = serialize_game_started_message(buff, GAME_STATE);
                        boost::asio::write((*sock_ptr.get()), boost::asio::buffer(buff, mess_len), ignored_error);

                        for (auto t : SERVER_STATE.turns)
                        {
                            uint32_t mess_len = serialize_turn_message(buff, t);
                            boost::asio::write((*sock_ptr.get()), boost::asio::buffer(buff, mess_len), ignored_error);
                        }
                    }
                    else {
                        for (auto pl : GAME_STATE.players)
                        {
                            uint32_t mess_len = serialize_accepted_player_message(buff, pl.second);
                            // sending list of accepted players
                            boost::asio::write((*sock_ptr.get()), boost::asio::buffer(buff, mess_len), ignored_error);
                        }
                    }
                }
                std::thread th{handle_connection, std::ref(conn_id)};
                threads.push_back(std::move(th));
            }
        }
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << '\n';
        exit(1);
    }
}

int main(int argc, char **argv)
{
    parse_input(argc, argv);

    std::thread connection_acceptor{accept_connections};

    handle_coordination();

    connection_acceptor.join();
    for (auto &th : threads)
        th.join();

    return 0;
}