/* Copyright 2015 SKA South Africa
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 */

#include <streambuf>
#include "send_streambuf.h"

namespace spead2
{
namespace send
{

streambuf_stream::streambuf_stream(
    boost::asio::io_service &io_service,
    std::streambuf &streambuf,
    const stream_config &config)
    : stream<streambuf_stream>(io_service, config), streambuf(streambuf)
{
}

} // namespace send
} // namespace spead2
