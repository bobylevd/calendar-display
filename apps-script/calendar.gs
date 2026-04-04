// ============================================================
// CONFIGURATION
// ============================================================

// Change this to your own random secret key
// Generate one at: https://randomkeygen.com (use a 256-bit key)
var SECRET_KEY = 'CHANGE_ME_TO_A_RANDOM_STRING';

// Calendars to display. Leave empty to use only the default calendar.
// To add more calendars, add their IDs (Settings > Calendar ID in Google Calendar):
// var CALENDAR_IDS = ['primary', 'work@group.calendar.google.com', 'family@group.calendar.google.com'];
var CALENDAR_IDS = [];

function doGet(e) {
  var key = e.parameter.key || '';
  if (key !== SECRET_KEY) {
    return ContentService.createTextOutput(JSON.stringify({error: 'unauthorized'}))
      .setMimeType(ContentService.MimeType.JSON);
  }

  try {
    var now = new Date();
    var endOfDay = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 59, 59);

    var calendars = [];
    if (CALENDAR_IDS.length === 0) {
      calendars.push(CalendarApp.getDefaultCalendar());
    } else {
      for (var c = 0; c < CALENDAR_IDS.length; c++) {
        var id = CALENDAR_IDS[c];
        var cal = (id === 'primary') ? CalendarApp.getDefaultCalendar() : CalendarApp.getCalendarById(id);
        if (cal) calendars.push(cal);
      }
    }

    var events = [];

    for (var ci = 0; ci < calendars.length; ci++) {
      var calEvents = calendars[ci].getEvents(now, endOfDay);
      for (var i = 0; i < calEvents.length; i++) {
        var ev = calEvents[i];
        var allDay = ev.isAllDayEvent();

        events.push({
          title: ev.getTitle(),
          start: allDay ? formatDate(ev.getAllDayStartDate()) : formatDateTime(ev.getStartTime()),
          end: allDay ? formatDate(ev.getAllDayEndDate()) : formatDateTime(ev.getEndTime()),
          allDay: allDay
        });
      }
    }

    events.sort(function(a, b) {
      if (a.allDay !== b.allDay) return a.allDay ? 1 : -1;
      return new Date(a.start) - new Date(b.start);
    });

    events = events.slice(0, 10);

    var output = JSON.stringify({
      events: events,
      count: events.length,
      nextUpdate: 60
    });

    return ContentService.createTextOutput(output)
      .setMimeType(ContentService.MimeType.JSON);
  } catch (err) {
    return ContentService.createTextOutput(JSON.stringify({error: err.message}))
      .setMimeType(ContentService.MimeType.JSON);
  }
}

function formatDateTime(date) {
  return Utilities.formatDate(date, Session.getScriptTimeZone(), "yyyy-MM-dd'T'HH:mm:ss");
}

function formatDate(date) {
  return Utilities.formatDate(date, Session.getScriptTimeZone(), "yyyy-MM-dd");
}
