
//          Copyright Oliver Kowalke 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "boost/fiber/round_robin.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include <boost/assert.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/scope_exit.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/thread.hpp>

#include "boost/fiber/detail/scheduler.hpp"
#include "boost/fiber/exceptions.hpp"

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_PREFIX
#endif

namespace boost {
namespace fibers {

bool fetch_ready( detail::worker_fiber::ptr_t & f)
{
    BOOST_ASSERT( ! f->is_running() );
    BOOST_ASSERT( ! f->is_terminated() );

    // set fiber to state_ready if dead-line was reached
    // set fiber to state_ready if interruption was requested
    if ( f->time_point() <= clock_type::now() || f->interruption_requested() )
        f->set_ready();
    return f->is_ready();
}

void
round_robin::resume_( detail::worker_fiber::ptr_t const& f)
{
    BOOST_ASSERT( f);
    BOOST_ASSERT( f->is_ready() );

    // store active fiber in local var
    detail::worker_fiber::ptr_t tmp = active_fiber_;
    // assign new fiber to active fiber
    active_fiber_ = f;
    // set active fiber to state_running
    active_fiber_->set_running();
    // resume active fiber
    active_fiber_->resume();
    // fiber is resumed

    BOOST_ASSERT( f == active_fiber_);
    // reset active fiber to previous
    active_fiber_ = tmp;
}

round_robin::round_robin() BOOST_NOEXCEPT :
    active_fiber_(),
    wqueue_(),
    rqueue_(),
    mn_()
{}

round_robin::~round_robin() BOOST_NOEXCEPT
{
    // fibers will be destroyed (stack-unwinding)
    // if last reference goes out-of-scope
    // therefore destructing wqueue_ && rqueue_
    // will destroy the fibers in this scheduler
    // if not referenced on other places
    while ( ! wqueue_.empty() && ! rqueue_.empty() )
        run();
}
void
round_robin::spawn( detail::worker_fiber::ptr_t const& f)
{ rqueue_.push( f); }

bool
round_robin::run()
{
    // move all fibers witch are ready (state_ready)
    // from waiting-queue to the runnable-queue
    wqueue_.move_to( rqueue_, fetch_ready);

    // pop new fiber from ready-queue which is not complete
    // (example: fiber in ready-queue could be canceled by active-fiber)
    detail::worker_fiber::ptr_t f;
    do
    {
        if ( rqueue_.empty() )
        {
            this_thread::yield();
            return false;
        }
        f = rqueue_.pop();
        if ( f->is_ready() ) break;
        else BOOST_ASSERT_MSG( false, "fiber with invalid state in ready-queue");
    }
    while ( true);

    // resume fiber
    resume_( f);

    return true;
}

void
round_robin::wait( unique_lock< detail::spinlock > & lk)
{ wait_until( clock_type::time_point( (clock_type::duration::max)() ), lk); }

bool
round_robin::wait_until( clock_type::time_point const& timeout_time,
                         unique_lock< detail::spinlock > & lk)
{
    BOOST_ASSERT( active_fiber_);
    BOOST_ASSERT( active_fiber_->is_running() );

    // set active fiber to state_waiting
    active_fiber_->set_waiting();
    // release lock
    lk.unlock();
    // push active fiber to wqueue_
    active_fiber_->time_point( timeout_time);
    wqueue_.push( active_fiber_);
    // suspend active fiber
    active_fiber_->suspend();
    // fiber is resumed

    return clock_type::now() < timeout_time;
}

void
round_robin::yield()
{
    BOOST_ASSERT( active_fiber_);
    BOOST_ASSERT( active_fiber_->is_running() );

    // set active fiber to state_waiting
    active_fiber_->set_ready();
    // push active fiber to wqueue_
    wqueue_.push( active_fiber_);
    // suspend acitive fiber
    active_fiber_->suspend();
    // fiber is resumed
}

void
round_robin::join( detail::worker_fiber::ptr_t const& f)
{
    BOOST_ASSERT( f);
    BOOST_ASSERT( f != active_fiber_);

    if ( active_fiber_)
    {
        // set active fiber to state_waiting
        active_fiber_->set_waiting();
        // push active fiber to wqueue_
        wqueue_.push( active_fiber_);
        // add active fiber to joinig-list of f
        if ( ! f->join( active_fiber_) )
            // f must be already terminated therefore we set
            // active fiber to state_ready
            // FIXME: better state_running and no suspend
            active_fiber_->set_ready();
        // suspend fiber until f terminates
        active_fiber_->suspend();
        // fiber is resumed by f
    }
    else
    {
        while ( ! f->is_terminated() )
        {
            // yield this thread if scheduler did not 
            // resumed some fibers in the previous round
            if ( ! run() ) this_thread::yield();
        }
    }

    BOOST_ASSERT( f->is_terminated() );
}

void
round_robin::priority( detail::worker_fiber::ptr_t const& f, int prio) BOOST_NOEXCEPT
{
    BOOST_ASSERT( f);

    // set only priority to fiber
    // round-robin does not respect priorities
    f->priority( prio);
}

}}

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_SUFFIX
#endif
