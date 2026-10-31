/*
 * Shared libraries on the local network, through multicast DNS.
 */
#include "ShareFinder.h"

#include <Message.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <map>

#include "BeTunes.h"
#include "DaapProtocol.h"


static const char* kServiceName = "_daap._tcp.local";
static const bigtime_t kAnswerTime = 1500000;
static const bigtime_t kFollowUpTime = 1000000;
static const bigtime_t kRoundDelays[] = { 3000000, 10000000 };
static const bigtime_t kRoundInterval = 30000000;


namespace {


void
send_query(int socket, const std::vector<DnsQuestion>& questions)
{
	static uint16_t sId = (uint16_t)(system_time() & 0xffff);
	std::string packet = dns_query(++sId, questions);
	struct sockaddr_in group;
	memset(&group, 0, sizeof(group));
	group.sin_family = AF_INET;
	group.sin_port = htons(5353);
	group.sin_addr.s_addr = htonl(0xe00000fb);
		// 224.0.0.251
	sendto(socket, packet.data(), packet.size(), 0,
		(struct sockaddr*)&group, sizeof(group));
}


struct Answer {
	DnsRecord	record;
	uint32		source;
		// the responder's address, host order
};


void
collect_answers(int socket, bigtime_t duration, std::vector<Answer>& answers,
	const std::atomic<bool>* quit)
{
	bigtime_t end = system_time() + duration;
	uint8_t packet[9000];
	while (system_time() < end && (quit == NULL || !*quit)) {
		struct pollfd poller = { socket, POLLIN, 0 };
		bigtime_t left = end - system_time();
		if (poll(&poller, 1, (int)std::min((bigtime_t)100, left / 1000 + 1))
				<= 0)
			continue;
		struct sockaddr_in from;
		socklen_t fromLength = sizeof(from);
		ssize_t size = recvfrom(socket, packet, sizeof(packet), 0,
			(struct sockaddr*)&from, &fromLength);
		if (size <= 0)
			continue;
		std::vector<DnsRecord> records;
		if (!dns_parse_reply(packet, size, records))
			continue;
		for (size_t i = 0; i < records.size(); i++) {
			Answer answer;
			answer.record = records[i];
			answer.source = ntohl(from.sin_addr.s_addr);
			answers.push_back(answer);
		}
	}
}


const Answer*
find_answer(const std::vector<Answer>& answers, const std::string& name,
	uint16_t type)
{
	for (size_t i = answers.size(); i-- > 0;) {
		if (answers[i].record.type == type
			&& dns_same_name(answers[i].record.name, name))
			return &answers[i];
	}
	return NULL;
}


}	// namespace


void
ShareFinder::FindShares(int socket, std::vector<Share>& shares,
	const std::atomic<bool>* quit)
{
	std::vector<Answer> answers;
	std::vector<DnsQuestion> questions;
	questions.push_back({ kServiceName, kDnsTypePtr });
	send_query(socket, questions);
	collect_answers(socket, kAnswerTime, answers, quit);

	// the shares, and a second question about what their answers left out
	std::vector<std::string> instances;
	questions.clear();
	for (size_t i = 0; i < answers.size(); i++) {
		const DnsRecord& record = answers[i].record;
		if (record.type != kDnsTypePtr || record.ttl == 0
			|| !dns_same_name(record.name, kServiceName))
			continue;
		bool known = false;
		for (size_t j = 0; j < instances.size(); j++)
			known |= dns_same_name(instances[j], record.target);
		if (known)
			continue;
		instances.push_back(record.target);

		const Answer* service = find_answer(answers, record.target,
			kDnsTypeSrv);
		if (service == NULL)
			questions.push_back({ record.target, kDnsTypeSrv });
		if (find_answer(answers, record.target, kDnsTypeTxt) == NULL)
			questions.push_back({ record.target, kDnsTypeTxt });
		if (service != NULL && find_answer(answers, service->record.target,
				kDnsTypeA) == NULL)
			questions.push_back({ service->record.target, kDnsTypeA });
	}
	if (!questions.empty() && (quit == NULL || !*quit)) {
		send_query(socket, questions);
		collect_answers(socket, kFollowUpTime, answers, quit);
		// a host name the second answers brought
		questions.clear();
		for (size_t i = 0; i < instances.size(); i++) {
			const Answer* service = find_answer(answers, instances[i],
				kDnsTypeSrv);
			if (service != NULL && find_answer(answers,
					service->record.target, kDnsTypeA) == NULL)
				questions.push_back({ service->record.target, kDnsTypeA });
		}
		if (!questions.empty() && (quit == NULL || !*quit)) {
			send_query(socket, questions);
			collect_answers(socket, kFollowUpTime, answers, quit);
		}
	}

	for (size_t i = 0; i < instances.size(); i++) {
		const Answer* service = find_answer(answers, instances[i],
			kDnsTypeSrv);
		if (service == NULL || service->record.port == 0)
			continue;
		Share share;
		share.name = dns_first_label(instances[i]);
		share.port = service->record.port;
		const Answer* address = find_answer(answers, service->record.target,
			kDnsTypeA);
		share.address = address != NULL ? address->record.address
			: service->source;
				// most share from the computer that answers for them
		share.password = false;
		const Answer* text = find_answer(answers, instances[i], kDnsTypeTxt);
		std::string value;
		if (text != NULL && dns_text_value(text->record.text, "Password",
				value))
			share.password = value == "true" || value == "1";
		shares.push_back(share);
	}
}


ShareFinder::ShareFinder(const BMessenger& target)
	:
	fTarget(target),
	fThread(-1),
	fWakeSem(create_sem(0, "share finder")),
	fQuit(false)
{
}


ShareFinder::~ShareFinder()
{
	fQuit = true;
	release_sem(fWakeSem);
	if (fThread >= 0) {
		status_t result;
		wait_for_thread(fThread, &result);
	}
	delete_sem(fWakeSem);
}


status_t
ShareFinder::Start()
{
	fThread = spawn_thread(&_ThreadEntry, "share finder", B_LOW_PRIORITY,
		this);
	if (fThread < 0)
		return fThread;
	return resume_thread(fThread);
}


void
ShareFinder::Refresh()
{
	release_sem(fWakeSem);
}


status_t
ShareFinder::_ThreadEntry(void* cookie)
{
	static_cast<ShareFinder*>(cookie)->_Run();
	return B_OK;
}


void
ShareFinder::_Run()
{
	int fd = -1;
	for (int32 round = 0; !fQuit; round++) {
		if (fd < 0) {
			fd = socket(AF_INET, SOCK_DGRAM, 0);
			if (fd >= 0) {
				struct sockaddr_in any;
				memset(&any, 0, sizeof(any));
				any.sin_family = AF_INET;
				bind(fd, (struct sockaddr*)&any, sizeof(any));
					// an ordinary port: the answers come to it directly
				unsigned char ttl = 255;
				setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
					sizeof(ttl));
			}
		}

		std::vector<Share> shares;
		if (fd >= 0)
			FindShares(fd, shares, &fQuit);
		if (fQuit)
			break;

		BMessage found(kMsgSharesFound);
		for (size_t i = 0; i < shares.size(); i++) {
			struct in_addr address;
			address.s_addr = htonl(shares[i].address);
			char host[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &address, host, sizeof(host));
			found.AddString("name", shares[i].name.c_str());
			found.AddString("host", host);
			found.AddInt32("port", shares[i].port);
			found.AddBool("password", shares[i].password);
		}
		if (fTarget.SendMessage(&found) != B_OK)
			break;

		bigtime_t delay = round < 2 ? kRoundDelays[round] : kRoundInterval;
		if (acquire_sem_etc(fWakeSem, 1, B_RELATIVE_TIMEOUT, delay)
				== B_BAD_SEM_ID)
			break;
		// a refresh, or several: one round for all of them
		int32 count;
		if (get_sem_count(fWakeSem, &count) == B_OK && count > 0)
			acquire_sem_etc(fWakeSem, count, B_RELATIVE_TIMEOUT, 0);
	}
	if (fd >= 0)
		close(fd);
}
