/*

  GPPS - General Purpose Permanent Storage

  A simple contract that allows the storage of arbitrary amounts of binary data
  into an Antelope blockchain's RAM.

  
  How it works
  ------------

  The contract has a single table, "nodes".

  The scope of the table is the user account that is operating on the table.
  All data entries created or destroyed by that account, all RAM spent, happens
  on entries that are under the scope of that account.

  The table's primary key is a 64-bit unsigned integer ID ("id"), and
  associated to an ID is an arbitrary amount of binary data ("data").
  
  A node is intended to store any amount of binary data that can be transmitted
  and processed in a single blockchain transaction. The maximum size of a
  transaction, and therefore of a node, is limited by the software and
  configuration employed by a specific blockchain network. However, it should
  be OK to try and store up to e.g. 8,192 bytes of binary data on each node.

  To actually set the byte content of a node, the "set" action should be used.
  The "owner" account must be authorized, as it will be the scope for the entry
  being created or updated.

  The "del" action can be called to remove a node, recovering all blockchain
  RAM expenditure. The "owner" account must be authorized, as it will be the
  scope of the entry being erased.

  To retrieve binary data from a node, you just use whatever method available
  to read tables from the blockchain (e.g. cleos get table). You will need the
  account name that is the scope for the data nodes, and the ID of the data
  node you want.

  Large files can be split in chunks that are stored as separate data nodes in
  a contiguous ID range.
  
  An entire scope can be marked as immutable by setting the data on its node 0
  to the hexadecimal value "DEAD". An immutable scope does not accept the
  redefinition or deletion of an existing node: all nodes are final and RAM
  spent on them cannot be recovered.


  Considerations
  --------------

  Node data can be large enough, in practice, that the various overheads
  associated with the mechanism do not end up being any more prohibitive than
  the consensus-data-archive idea already is in the first place. E.g. to store
  small (<1MB) and valuable ($1/KB) data files "permanently", this seems to
  work quite well.

  The data is stored in binary, but it is transmitted and retrieved in a text
  "bytes" format (the "ABI type"), which is the representation by the JSON
  serialization and deserialization interface of a vector of bytes data type.
  That textual representation is in hexadecimal characters, meaning the
  network resource expenditure is double that of the binary storage in RAM
  for the user data itself. Fortunately, that bloat only applies to network
  propagation and block data storage, which are two resources that regenerate
  (blocks can be pruned by snapshotting and communication costs are transient).


  Simple examples
  ---------------

  Pushing entire "node.br" file as single node (using the contract account keys
  and RAM allocation itself to test on UX Network):

  cleos --url https://api.uxnetwork.io push action datastoreutx set
  '{"owner":"datastoreutx", "id":"1", "data":"'$(xxd -p -c 9999999 node.br)'"}'
  -p datastoreutx@active

  Retrieving that single node on UX Network and writing it as "no.br":

  cleos --url https://api.uxnetwork.io get table datastoreutx datastoreutx
  nodes -L 1 -U 1 | jq '.rows[0].data' | tr -d '"' | xxd -r -p > no.br

  Check that they are identical binary brotli archive files:

  diff node.br no.br

  Set scope "myaccountnam" on UX Network to immutable:
  
  cleos --url https://api.uxnetwork.io push action datastoreutx set
  '{"owner":"myaccountnam", "id":"0", "data":"DEAD"}' -p myaccountnam@active
  
*/

#include <eosio/eosio.hpp>

using namespace eosio;

using namespace std;

class [[eosio::contract]] gpps : public contract {
public:
  using contract::contract;

  struct [[eosio::table]] node {
    uint64_t                id;
    vector<unsigned char>   data;
    uint64_t primary_key() const { return id; }
  };

  typedef eosio::multi_index< "nodes"_n, node > nodes;

  /*
    This writes data on a node, allocating it if necessary.

    Setting node 0 with empty data is a special case that signals the entire
    scope is immutable -- data cannot be redefined once set.
   */
  [[eosio::action]]
  void set( name owner, uint64_t id, vector<unsigned char> data ) {
    require_auth( owner );
    nodes nds( _self, owner.value );
    auto it = nds.find( id );
    if (it == nds.end()) {
      nds.emplace( owner, [&]( auto& n ) {
	n.id = id;
	n.data = data;
      });
    } else {
      check( !immutable( owner ), "Immutable scope." );
      const auto& nd = *it;
      nds.modify( it, same_payer, [&]( auto& n ) {
	n.data = data;
      });
    }
  }

  /*
    This erases a node.
  */
  [[eosio::action]]
  void del( name owner, uint64_t id ) {
    require_auth( owner );
    nodes nds( _self, owner.value );
    auto it = nds.find( id );
    check( it != nds.end(), "Node does not exist." );
    check( !immutable( owner ), "Immutable scope." );
    nds.erase( it );
  }

private:
  
  /*
    A scope is flagged as immutable when its node 0 is set to 0xDEAD.
  */
  bool immutable( name owner ) {
    nodes nds( _self, owner.value );
    auto it = nds.find( 0 );
    if (it == nds.end())
      return false;
    const auto& nd = *it;
    return (nd.data.size() == 2
	    && nd.data[0] == 222   // 0xDE
	    && nd.data[1] == 173); // 0xAD
  }
};
