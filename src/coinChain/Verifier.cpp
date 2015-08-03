/* -*-c++-*- libcoin - Copyright (C) 2012 Michael Gronager
 *
 * libcoin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * libcoin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libcoin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <coinChain/Verifier.h>

#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp> 

using namespace std;
using namespace boost;

Verifier::Verifier(size_t threads) : _work(_io_service), _failed(false) {
    while (threads--)
        _threads.create_thread(boost::bind(&boost::asio::io_service::run, &_io_service));
}

Verifier::~Verifier() {
    _io_service.stop();
    _threads.join_all();
}

void Verifier::reset() {
    boost::unique_lock< boost::shared_mutex > lock(_state_access);
    _reason = "";
    _failed = false;
    _pending_verifications.clear();
}

void Verifier::verify(const vector<Script>& scripts, const Transaction& txn, bool strictPayToScriptHash) {
    if (_threads.size()) {
        typedef packaged_task<bool> bool_task;
        boost::shared_ptr<bool_task> task = boost::make_shared<bool_task>(boost::bind(&Verifier::do_verify, this, scripts, txn, strictPayToScriptHash));
        unique_future<bool> fut = task->get_future();
        _pending_verifications.push_back(boost::move(fut));
        _io_service.post(boost::bind(&bool_task::operator(), task));
    }
    else {
        if (!_failed)
            _failed = !txn.verify(scripts, strictPayToScriptHash);
        if (_failed)
            failed_with_reason("Transaction hash: " + txn.getHash().toString());
    }
}

bool Verifier::do_verify(const vector<Script>& scripts, const Transaction& txn, bool strictPayToScriptHash) {
    if (already_failed()) // no reason to waste time on a loosing tx
        return true;
    
    if (!txn.verify(scripts, strictPayToScriptHash)) {
        failed_with_reason("Transaction hash: " + txn.getHash().toString());
        return false;
    }
    
    return true;
}

void Verifier::failed_with_reason(std::string reason) {
    boost::unique_lock< shared_mutex > lock(_state_access);
    _failed = true;
    _reason = reason;
}

bool Verifier::already_failed() const {
    shared_lock< shared_mutex > lock(_state_access);
    return _failed;
}

bool Verifier::yield_success() const {
    // wait for all threads to exit
    wait_for_all(_pending_verifications.begin(), _pending_verifications.end());
    return !already_failed();
}
