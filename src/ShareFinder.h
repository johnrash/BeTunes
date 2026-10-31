/*
 * Finds the music libraries shared on the local network: asks multicast DNS
 * for _daap._tcp now and then (Haiku has no mDNS service to ask) and sends
 * its target what answered, every round, as kMsgSharesFound: "name",
 * "host" (an IPv4 address), "port" and "password" (the share says it needs
 * one), one of each per share.
 */
#ifndef SHARE_FINDER_H
#define SHARE_FINDER_H


#include <Messenger.h>
#include <OS.h>

#include <atomic>
#include <string>
#include <vector>


struct DnsRecord;


class ShareFinder {
public:
								ShareFinder(const BMessenger& target);
								~ShareFinder();

			status_t			Start();
	// Asks again now (the rounds are half a minute apart otherwise).
			void				Refresh();

	struct Share {
			std::string			name;
			uint32				address;
				// host order
			uint16				port;
			bool				password;
	};

	// One round: the query, the answers, and the questions about what the
	// answers left out. Public for the probe in tests/.
	static	void				FindShares(int socket,
									std::vector<Share>& shares,
									const std::atomic<bool>* quit = NULL);

private:
	static	status_t			_ThreadEntry(void* cookie);
			void				_Run();

			BMessenger			fTarget;
			thread_id			fThread;
			sem_id				fWakeSem;
			std::atomic<bool>	fQuit;
};


#endif	// SHARE_FINDER_H
