# libguestfs Ruby bindings -*- ruby -*-
# Copyright (C) 2013-2016 Red Hat Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

require File::join(File::dirname(__FILE__), 'test_helper')

class TestLoad < MiniTest::Unit::TestCase
  def test_create_multiple
    g1 = Guestfs::Guestfs.new()
    g2 = Guestfs::Guestfs.new()
    g3 = Guestfs::Guestfs.new()
    refute_nil (g1)
    refute_nil (g2)
    refute_nil (g3)
  end
end