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
namespace po = boost::program_options;
using boost::asio::ip::tcp;
using boost::asio::ip::udp;
using boost::asio::ip::address;

#define BUFFER_LENGTH 65507
unsigned char FROM_SERVER_BUFFER[BUFFER_LENGTH];
unsigned char FROM_GUI_BUFFER[BUFFER_LENGTH];
unsigned char TO_SERVER_BUFFER[BUFFER_LENGTH];
unsigned char TO_GUI_BUFFER[BUFFER_LENGTH];
bool should_disconnect = false;
bool has_joined = false;
std::mutex should_disconnect_mutex;
std::mutex has_joined_mutex;
std::mutex game_state_mutex;

struct client_parameters
{
    std::string name;
    std::string server_addr;
    std::string gui_addr;
    std::string server_port_str;
    std::string gui_port_str;
    uint16_t server_port;
    uint16_t gui_port;
    uint16_t port;
    bool is_gui_addr_v4;
};

client_parameters PARAMS;
game_state GAME_STATE;


// extracts first and second part from string (host name):(port)
// or (IPv4):(port) or (IPv6):(port)
std::pair<std::string, std::string> split_address_string(std::string str)
{
    size_t len = str.length();
    int port_beg = len - 1;
    while (port_beg >= 0)
    {
        if (str[port_beg] == ':')
        {
            break;
        }
        port_beg--;
    }
    port_beg++;

    std::string port_str = str.substr(port_beg, len);
    std::string addr_str = str.substr(0, port_beg - 1);

    return {addr_str, port_str};
}

uint16_t string_to_port(std::string port_str)
{
    unsigned short port_uint;
    try
    {
        port_uint = boost::lexical_cast<unsigned short>(port_str);
    }
    catch (std::exception &e)
    {
        std::cerr << port_str << " is not a correct port value.\n";
        exit(1);
    }
    return port_uint;
}

void parse_input(int argc, char **argv)
{
    po::options_description desc("Program usage");
    po::variables_map vm;
    desc.add_options()("help,h", "produce help message")("gui-address,d", po::value<std::string>(), "set gui address")("server-address,s", po::value<std::string>(), "set server address")("player-name,n", po::value<std::string>(), "set player name")("port,p", po::value<uint16_t>(), "set port where client listens to GUI communicates");

    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << "\n";
        exit(1);
    }

    if (!vm.count("gui-address") || !vm.count("server-address") ||
        !vm.count("player-name") || !vm.count("port"))
    {
        std::cerr << "All of the following parameters excluding help are necessary:\n"
                  << desc << "\n";
        exit(1);
    }

    PARAMS.name = vm["player-name"].as<std::string>();
    PARAMS.port = vm["port"].as<uint16_t>();

    auto gui_params = split_address_string(vm["gui-address"].as<std::string>());
    PARAMS.gui_addr = gui_params.first;
    PARAMS.gui_port_str = gui_params.second;
    PARAMS.gui_port = string_to_port(gui_params.second);
    auto server_params = split_address_string(vm["server-address"].as<std::string>());
    PARAMS.server_addr = server_params.first;
    PARAMS.server_port_str = server_params.second;
    PARAMS.server_port = string_to_port(server_params.second);

    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(PARAMS.gui_addr, ec);
    if (ec)
        PARAMS.is_gui_addr_v4 = false;
    else
        PARAMS.is_gui_addr_v4 = addr.is_v4(); 
}

bool is_gui_message_correct(size_t len, unsigned char *ptr)
{
    if (len == 0)
    {
        return false;
    }
    if (len != PLACE_MSG_SIZE && len != MOVE_MSG_SIZE)
        return false;
    if (*ptr > MOVE_GUI)
        return false;
    if (len == MOVE_MSG_SIZE)
    {
        ptr++;
        return (*ptr <= LEFT);
    }
    return true;
}

size_t prepare_message_to_sever(uint8_t type)
{
    if (type > MOVE)
        return 0; // incorrect message
    TO_SERVER_BUFFER[0] = type;
    if (type == JOIN)
    {
        serialize(PARAMS.name, TO_SERVER_BUFFER + 1);
        return PARAMS.name.length() + 2;
    }
    if (type < MOVE)
        return PLACE_MSG_SIZE;
    TO_SERVER_BUFFER[1] = FROM_GUI_BUFFER[1];
    return MOVE_MSG_SIZE;
}

// functions execute take structs that represent correct messages from server
// and modify client's state so it is compatible with server

void execute(hello_msg hm)
{
    GAME_STATE.server_name = hm.server_name;
    GAME_STATE.players_count = hm.players_count;
    GAME_STATE.size_x = hm.size_x;
    GAME_STATE.size_y = hm.size_y;
    GAME_STATE.game_length = hm.game_length;
    GAME_STATE.explosion_radius = hm.explosion_radius;
    GAME_STATE.bomb_timer = hm.bomb_timer;
}

void execute(accepted_player_msg ap)
{
    GAME_STATE.players.insert({ap.player_id, ap.pl});
    GAME_STATE.scores.insert({ap.player_id, 0});
}

void execute(game_started_msg gs)
{   
    GAME_STATE.started = true;
    for (auto el : gs.players)
    {
        GAME_STATE.players.insert(el);
        GAME_STATE.scores.insert({el.first, 0});
    }
}

void execute(bomb_placed_ev bp)
{
    bomb b = {bp.bomb_id, bp.pos, GAME_STATE.bomb_timer};
    GAME_STATE.bombs.insert({bp.bomb_id, b});
}

void explode(position pos)
{
    for (uint16_t i = 0; i <= GAME_STATE.explosion_radius; i++)
    {
        uint16_t px = pos.x + i;
        GAME_STATE.explosions.insert({px, pos.y});
        if (px == GAME_STATE.size_x - 1) // edge of the board
            break;
        if (GAME_STATE.blocks.count({px, pos.y})) // block
            break;
    }
    for (uint16_t i = 0; i <= GAME_STATE.explosion_radius; i++)
    {
        uint16_t px = pos.x - i;
        GAME_STATE.explosions.insert({px, pos.y});
        if (px == 0) // edge of the board
            break;
        if (GAME_STATE.blocks.count({px, pos.y})) // block
            break;
    }
    for (uint16_t i = 0; i <= GAME_STATE.explosion_radius; i++)
    {
        uint16_t py = pos.y + i;
        GAME_STATE.explosions.insert({pos.x, py});
        if (py == GAME_STATE.size_y - 1) // edge of the board
            break;
        if (GAME_STATE.blocks.count({pos.x, py})) // block
            break;
    }
    for (uint16_t i = 0; i <= GAME_STATE.explosion_radius; i++)
    {
        uint16_t py = pos.y - i;
        GAME_STATE.explosions.insert({pos.x, py});
        if (py == 0) // edge of the board
            break;
        if (GAME_STATE.blocks.count({pos.x, py})) // block
            break;
    }
}

void execute(bomb_exploded_ev be, std::set<uint8_t> &killed, std::set<position> &destroyed)
{
    position pos = GAME_STATE.bombs[be.bomb_id].pos;
    explode(pos);
    GAME_STATE.bombs.erase(be.bomb_id);
    for (auto pl_id : be.robots_destroyed)
    {
        killed.insert(pl_id);
    }
    for (auto block : be.blocks_destroyed)
    {
        destroyed.insert(block);
    }
}

void execute(player_moved_ev pm)
{
    GAME_STATE.player_positions[pm.player_id] = pm.pos;
}

void execute(block_placed_ev bp)
{
    GAME_STATE.blocks.insert(bp.pos);
}

void execute(turn_msg t)
{
    GAME_STATE.turn = t.turn;
    std::set<uint8_t> killed;
    std::set<position> destroyed;
    for (auto &b : GAME_STATE.bombs)
        b.second.timer--;
    for (auto ev : t.bomb_pl)
        execute(ev);
    for (auto ev : t.bomb_ex)
        execute(ev, killed, destroyed);
    for (auto pl_id : killed)
        GAME_STATE.scores[pl_id]++;
    for (auto bl : destroyed)
        GAME_STATE.blocks.erase(bl);
    for (auto ev : t.pl_mov)
        execute(ev);
    for (auto ev : t.block_pl)
        execute(ev);
    
}

void execute(game_ended_msg)
{
    GAME_STATE.started = false; // going back to lobby
    GAME_STATE.players.clear();
    GAME_STATE.player_positions.clear();
    GAME_STATE.scores.clear();
    GAME_STATE.bombs.clear();
    GAME_STATE.blocks.clear();
    GAME_STATE.turn = 0;
    const std::lock_guard<std::mutex> lock(has_joined_mutex);
    has_joined = false;
}

void deserialize_and_execute(unsigned char *ptr)
{
    if (*ptr == HELLO)
    {
        hello_msg hm = deserialize_hello_message(ptr);
        const std::lock_guard<std::mutex> lock(game_state_mutex);
        execute(hm);
    }
    else if (*ptr == ACCEPTED_PLAYER)
    {
        accepted_player_msg ap = deserialize_accepted_player_message(ptr);
        const std::lock_guard<std::mutex> lock(game_state_mutex);
        execute(ap);
    }
    else if (*ptr == GAME_STARTED)
    {
        game_started_msg gs = deserialize_game_started_message(ptr);
        const std::lock_guard<std::mutex> lock(game_state_mutex);
        execute(gs);
    }
    else if (*ptr == TURN)
    {
        turn_msg t = deserialize_turn_message(ptr);
        const std::lock_guard<std::mutex> lock(game_state_mutex);
        execute(t);
    }
    else if (*ptr == GAME_ENDED)
    {
        game_ended_msg ge = deserialize_game_ended_message(ptr);
        const std::lock_guard<std::mutex> lock(game_state_mutex);
        execute(ge);
    }
}

// functions read_exact takes an exact number of bytes from TCP connection with server
// so that the input correspond with one message from server;
// the message is written into place pointed by ptr (FROM_SERVER_BUFFER)

unsigned char temp_buff[BUFFER_LENGTH];

unsigned char *read_exact_number_of_bytes(tcp::socket &tcp_socket, unsigned char *ptr, size_t n)
{
    boost::system::error_code err;
    size_t read_bytes = boost::asio::read(tcp_socket, boost::asio::buffer(temp_buff),
                                          boost::asio::transfer_exactly(n), err);
    if (err)
    {
        std::cerr << "error while reading message from tcp. read " << read_bytes << " instead of bytes " << n << ". client will disconnect\n";
        const std::lock_guard<std::mutex> lock(should_disconnect_mutex);
        should_disconnect = true;
        exit(1);
    }
    assert(read_bytes == n);
    for (uint8_t i = 0; i < n; i++)
    {
        *ptr = temp_buff[i];
        ptr++;
    }
    return ptr;
}

unsigned char *read_exact_string(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 1); // string length
    uint8_t len = temp_buff[0];
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, len);
    return ptr;
}

unsigned char *read_exact_player(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_string(tcp_socket, ptr); // name
    ptr = read_exact_string(tcp_socket, ptr); // addres
    return ptr;
}

unsigned char *read_exact_hello(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_string(tcp_socket, ptr); // server name
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 11);
    return ptr;
}

unsigned char *read_exact_accepted_player(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 1); // player id
    ptr = read_exact_player(tcp_socket, ptr);
    return ptr;
}

unsigned char *read_exact_game_started(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 4); // map size
    uint32_t len = deserialize_number(4, temp_buff);
    for (uint32_t i = 0; i < len; i++)
    {
        ptr = read_exact_accepted_player(tcp_socket, ptr);
    }
    return ptr;
}

unsigned char *read_exact_game_ended(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 4); // map size
    uint32_t len = deserialize_number(4, temp_buff);
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 5 * len);
    return ptr;
}

unsigned char *read_exact_bomb_exploaded(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 4); // bomb id
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 4); // robots_destroyed list length
    uint32_t len = deserialize_number(4, temp_buff);
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, len); // destroyed robots
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 4);   // blocks_destroyed list length
    len = deserialize_number(4, temp_buff);
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, len * 4);
    return ptr;
}

unsigned char *read_exact_event(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 1); // event type
    uint8_t type = temp_buff[0];
    if (type == BOMB_PLACED)
    {
        ptr = read_exact_number_of_bytes(tcp_socket, ptr, 8);
    }
    else if (type == BOMB_EXPLODED)
    {
        ptr = read_exact_bomb_exploaded(tcp_socket, ptr);
    }
    else if (type == PLAYER_MOVE)
    {
        ptr = read_exact_number_of_bytes(tcp_socket, ptr, 5);
    }
    else if (type == BLOCK_PLACED)
    {
        ptr = read_exact_number_of_bytes(tcp_socket, ptr, 4);
    }
    else
    {
        std::cerr << "incorrect event type:" << type << " in message from server. client will disconnect\n";
        const std::lock_guard<std::mutex> lock(should_disconnect_mutex);
        should_disconnect = true;
        exit(1);
    }
    return ptr;
}

unsigned char *read_exact_turn(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 2); // turn
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 4); // list length
    uint32_t len = deserialize_number(4, temp_buff);
    for (uint32_t i = 0; i < len; i++)
    {
        ptr = read_exact_event(tcp_socket, ptr);
    }
    return ptr;
}

void read_one_message(tcp::socket &tcp_socket, unsigned char *ptr)
{
    ptr = read_exact_number_of_bytes(tcp_socket, ptr, 1); // message type
    uint8_t type = temp_buff[0];
    if (type == HELLO)
    {
        read_exact_hello(tcp_socket, ptr);
    }
    else if (type == ACCEPTED_PLAYER)
    {
        read_exact_accepted_player(tcp_socket, ptr);
    }
    else if (type == GAME_STARTED)
    {
        read_exact_game_started(tcp_socket, ptr);
    }
    else if (type == TURN)
    {
        read_exact_turn(tcp_socket, ptr);
    }
    else if (type == GAME_ENDED)
    {
        read_exact_game_ended(tcp_socket, ptr);
    }
    else
    {
        std::cerr << "client received incorrect message from server and will disconnect\n";
        const std::lock_guard<std::mutex> lock(should_disconnect_mutex);
        should_disconnect = true;
        exit(1);
    }
}

boost::asio::io_context io_context;

std::shared_ptr<tcp::socket> connect_to_tcp() {
    
    tcp::resolver resolver(io_context);
    tcp::resolver::results_type endpoints =
        resolver.resolve(PARAMS.server_addr, PARAMS.server_port_str);
    auto tcp_socket = std::make_shared<tcp::socket>(io_context);
    boost::asio::connect((*tcp_socket.get()), endpoints);
    (*tcp_socket.get()).set_option(tcp::no_delay(true));
    return tcp_socket;
}

void handle_gui_server_communication(std::shared_ptr<tcp::socket> tcp_socket)
{
    try
    {
        udp::socket udp_socket(io_context, udp::endpoint{udp::v6(), PARAMS.port});
        udp::endpoint client;

        for (;;)
        {
            {
                const std::lock_guard<std::mutex> lock(should_disconnect_mutex);
                if (should_disconnect)
                    break;
            }

            size_t received_len = udp_socket.receive_from(boost::asio::buffer(FROM_GUI_BUFFER), client);

            // ignoring incorrect messages from GUI (or shutting down if received 0 bytes)
            if (!is_gui_message_correct(received_len, FROM_GUI_BUFFER))
                continue;

            uint8_t mess_type;
            {
                const std::lock_guard<std::mutex> lock(has_joined_mutex);
                if (!has_joined)
                {
                    mess_type = JOIN;
                    has_joined = true;    //client became player
                }
                else
                    mess_type = FROM_GUI_BUFFER[0] + 1;
            }

            size_t send_len = prepare_message_to_sever(mess_type);
            boost::system::error_code ignored_error;
            boost::asio::write((*tcp_socket.get()), boost::asio::buffer(TO_SERVER_BUFFER, send_len), ignored_error);
        }
        boost::system::error_code ec;
        udp_socket.shutdown(udp::socket::shutdown_both, ec);
        udp_socket.close();
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        const std::lock_guard<std::mutex> lock(should_disconnect_mutex);
        should_disconnect = true;
        exit(1);
    }
}

void handle_server_gui_communication(std::shared_ptr<tcp::socket> tcp_socket)
{
    try
    {
        udp::resolver udp_resolver(io_context);
        udp::socket udp_socket{io_context};
        udp::endpoint gui_endpoint;
        if (PARAMS.is_gui_addr_v4) {
            gui_endpoint = *udp_resolver.resolve(udp::v4(), PARAMS.gui_addr, PARAMS.gui_port_str).begin();
            udp_socket.open(udp::v4());
        }
        else {
            gui_endpoint = *udp_resolver.resolve(udp::v6(), PARAMS.gui_addr, PARAMS.gui_port_str).begin();
            udp_socket.open(udp::v6());
        }

        boost::system::error_code error;
        for (;;)
        {
            {
                const std::lock_guard<std::mutex> lock(should_disconnect_mutex);
                if (should_disconnect)
                    break;
            }

            read_one_message((*tcp_socket.get()), FROM_SERVER_BUFFER);
            deserialize_and_execute(FROM_SERVER_BUFFER);

            bool smth_to_send = false;
            size_t send_len;
            if (FROM_SERVER_BUFFER[0] == ACCEPTED_PLAYER || FROM_SERVER_BUFFER[0] == GAME_ENDED)     //TODO: rlly?
            {
                const std::lock_guard<std::mutex> lock(game_state_mutex);
                send_len = serialize_lobby_message(TO_GUI_BUFFER, GAME_STATE);
                smth_to_send = true;
            }
            if (FROM_SERVER_BUFFER[0] == HELLO)
            {
                const std::lock_guard<std::mutex> lock(game_state_mutex);
                if (GAME_STATE.started)
                    continue;
                send_len = serialize_lobby_message(TO_GUI_BUFFER, GAME_STATE);
                smth_to_send = true;
            }
            if (FROM_SERVER_BUFFER[0] == TURN)
            {
                const std::lock_guard<std::mutex> lock(game_state_mutex);
                send_len = serialize_game_message(TO_GUI_BUFFER, GAME_STATE);
                GAME_STATE.explosions.clear();
                smth_to_send = true;
            }

            if (smth_to_send)
            {
                udp_socket.send_to(boost::asio::buffer(TO_GUI_BUFFER, send_len), gui_endpoint);
            }
        }
        boost::system::error_code ec;
        udp_socket.shutdown(udp::socket::shutdown_both, ec);
        udp_socket.close();
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        const std::lock_guard<std::mutex> lock(should_disconnect_mutex);
        should_disconnect = true;
        exit(1);
    }
}

int main(int argc, char **argv)
{
    parse_input(argc, argv);
    auto tcp_sock = connect_to_tcp();
    std::thread t1{handle_gui_server_communication, std::ref(tcp_sock)};
    std::thread t2{handle_server_gui_communication, std::ref(tcp_sock)};
    t2.join();
    t1.join();
    boost::system::error_code ec;
    (*tcp_sock.get()).shutdown(tcp::socket::shutdown_both, ec);
    (*tcp_sock.get()).close();
    return 0;
}