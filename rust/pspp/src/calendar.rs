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

use chrono::{Datelike, Days, Month, NaiveDate, NaiveDateTime, NaiveTime};
use num::FromPrimitive;
use thiserror::Error as ThisError;

use crate::format::Settings;

const EPOCH: NaiveDate = NaiveDate::from_ymd_opt(1582, 10, 14).unwrap();
const EPOCH_DATETIME: NaiveDateTime = EPOCH.and_time(NaiveTime::MIN);

pub fn date_time_to_pspp(date_time: NaiveDateTime) -> f64 {
    (date_time - EPOCH_DATETIME).as_seconds_f64()
}

/// Takes a count of days from 14 Oct 1582 and translates it into a Gregorian
/// calendar date, if possible.  Positive and negative offsets are supported.
pub fn calendar_offset_to_gregorian(offset: f64) -> Option<NaiveDate> {
    let offset = offset as i64;
    if offset >= 0 {
        EPOCH.checked_add_days(Days::new(offset as u64))
    } else {
        EPOCH.checked_sub_days(Days::new(offset as u64))
    }
}

/// Returns the day of the year, where January 1 is day 1.
pub fn day_of_year(date: NaiveDate) -> Option<u32> {
    let january1 = NaiveDate::from_ymd_opt(date.year(), 1, 1)?;
    let delta = date - january1;
    Some(delta.num_days() as u32 + 1)
}

/// Returns the name for a month as a 3-character all-caps string.
pub fn short_month_name(month: u32) -> Option<&'static str> {
    let name = match month {
        1 => "JAN",
        2 => "FEB",
        3 => "MAR",
        4 => "APR",
        5 => "MAY",
        6 => "JUN",
        7 => "JUL",
        8 => "AUG",
        9 => "SEP",
        10 => "OCT",
        11 => "NOV",
        12 => "DEC",
        _ => return None,
    };
    Some(name)
}

/// Returns the name for a month as an all-caps string.
pub fn month_name(month: u32) -> Option<&'static str> {
    let name = match month {
        1 => "JANUARY",
        2 => "FEBRUARY",
        3 => "MARCH",
        4 => "APRIL",
        5 => "MAY",
        6 => "JUNE",
        7 => "JULY",
        8 => "AUGUST",
        9 => "SEPTEMBER",
        10 => "OCTOBER",
        11 => "NOVEMBER",
        12 => "DECEMBER",
        _ => return None,
    };
    Some(name)
}

#[derive(Copy, Clone, Debug, ThisError, PartialEq, Eq)]
pub enum DateError {
    /// Date is too early.
    #[error("Date {y:04}-{m:02}-{d:02} is before the earliest supported date 1582-10-15.")]
    InvalidDate { y: i32, m: i32, d: i32 },

    /// Invalid month.
    #[error("Month {0} is not in the acceptable range of 0 to 13, inclusive.")]
    InvalidMonth(i32),

    /// Invalid day.
    #[error("Day {0} is not in the acceptable range of 0 to 31, inclusive.")]
    InvalidDay(i32),
}

pub fn calendar_gregorian_adjust(
    y: i32,
    m: i32,
    d: i32,
    settings: &Settings,
) -> Result<(i32, i32, i32), DateError> {
    let y = settings.epoch.apply(y);

    let (y, m) = match m {
        0 => (y - 1, 12),
        1..=12 => (y, m),
        13 => (y + 1, 1),
        _ => return Err(DateError::InvalidMonth(m)),
    };

    if !(0..=31).contains(&d) {
        Err(DateError::InvalidDay(d))
    } else if y < 1582 || (y == 1582 && (m < 10 || (m == 10 && d < 15))) {
        Err(DateError::InvalidDate { y, m, d })
    } else {
        Ok((y, m, d))
    }
}

pub fn is_leap_year(y: i32) -> bool {
    NaiveDate::from_yo_opt(y, 1).unwrap().leap_year()
}

pub fn days_in_month(year: i32, month: i32) -> i32 {
    Month::from_i32(month)
        .unwrap()
        .num_days(year)
        .unwrap()
        .into()
}

pub fn calendar_raw_gregorian_to_offset(y: i32, m: i32, d: i32) -> i32 {
    -577735 + 365 * (y - 1) + (y - 1) / 4 - (y - 1) / 100
        + (y - 1) / 400
        + (367 * m - 362) / 12
        + if m <= 2 {
            0
        } else if m >= 2 && is_leap_year(y) {
            -1
        } else {
            -2
        }
        + d
}

/// Returns the number of days from 14 Oct 1582 to `(y,m,d)` in the Gregorian
/// calendar.  Returns an error for dates before 14 Oct 1582.
pub fn calendar_gregorian_to_offset(
    y: i32,
    m: i32,
    d: i32,
    settings: &Settings,
) -> Result<i32, DateError> {
    let (y, m, d) = calendar_gregorian_adjust(y, m, d, settings)?;
    Ok(calendar_raw_gregorian_to_offset(y, m, d))
}
