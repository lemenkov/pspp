// PSPP - a program for statistical analysis.
// Copyright (C) 2025 Free Software Foundation, Inc.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program.  If not, see <http://www.gnu.org/licenses/>.

use std::{borrow::Cow, sync::Arc};

use super::{page::Setup, Item};

// An output driver.
pub trait Driver {
    fn name(&self) -> Cow<'static, str>;

    fn write(&mut self, item: &Arc<Item>);

    /// Returns false if the driver doesn't support page setup.
    fn setup(&mut self, page_setup: &Setup) -> bool {
        let _ = page_setup;
        false
    }

    /// Ensures that anything written with [Self::write] has been displayed.
    ///
    /// This is called from the text-based UI before showing the command prompt,
    /// to ensure that the user has actually been shown any preceding output If
    /// it doesn't make sense for this driver to be used this way, then this
    /// function need not do anything.
    fn flush(&mut self) {}

    /// Ordinarily, the core driver code will skip passing hidden output items
    /// to [Self::write].  If this returns true, the core driver hands them to
    /// the driver to let it handle them itself.
    fn handles_show(&self) -> bool {
        false
    }

    /// Ordinarily, the core driver code will flatten groups of output items
    /// before passing them to [Self::write].  If this returns true, the core
    /// driver code leaves them in place for the driver to handle.
    fn handles_groups(&self) -> bool {
        false
    }
}

/*
/// An abstract way for the output subsystem to create an output driver.
trait DriverFactory {
    /// The file extension, without the leading dot, e.g. "pdf".
    fn extension(&self) ->  OsString;

    /// The default file name, including extension.
    ///
    /// If this is `-`, that implies that by default output will be directed to
    /// stdout.
    fn default_file_name(&self) -> PathBuf;

    /// Creates a new output driver of this class.  `name` and `type` should be
    /// passed directly to output_driver_init.
    ///
    /// It is up to the driver class to decide how to interpret `options`.  The
    /// create function should delete pairs that it understands from `options`,
    /// because the caller may issue errors about unknown options for any pairs
    /// that remain.
    fn create(&self, file_handle: (),

                                     enum settings_output_devices type,
                                     struct driver_options *);

}
*/
