/*
TRANSPORT_ADDRESS_ORDER.C (test)

The transport and the distributed netcode keep an address as the host's number (100.94.51.88 is 0x645E3358) and
a port likewise, while a socket's are in network order. get_endpoint_address (the connection's far end, which the server
files its machine under) and distributed_real_address swapped between the two with an inline swap, right only on a
little-endian host: on the Mac's PowerPC the G4 host filed its client under 88.51.94.100 and sent it nothing, and took
none of its datagrams. Both now convert with halo_be32 and halo_be16. The statements are taken from the sources and run
as this host (little-endian) and as a big-endian machine would run them (halo_be32 and halo_be16 the identity, a socket's
value read from the bytes in big-endian order).
*/

#include "harness.h"
#include "snippets.inc"

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("endpoint-address")
	{
		struct transport_address address;

		fill_endpoint_address(&address);
		CHECK(address.address.long_words[0] == 0x645E3358UL, "address %08lx", address.address.long_words[0]);
	}
	CASE("endpoint-port")
	{
		struct transport_address address;

		fill_endpoint_address(&address);
		CHECK(address.port == 0x13BE, "port %04x", address.port);
	}
	CASE("real-address")
	{
		CHECK(distributed_real_address(0x64400102UL) == 0xC0A84433UL, "real %08lx", distributed_real_address(0x64400102UL));
		CHECK(distributed_real_address(0xC0A84433UL) == 0xC0A84433UL, "an ordinary address changed");
	}

	return 0;
}
