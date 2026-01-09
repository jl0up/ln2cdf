"""
Reads data from Google Sheets and auto-refreshes the plot every 5 minutes
Uses Dash for live-updating web interface

requirements:
------------
pandas>=2.0.0
plotly>=5.17.0
dash>=2.14.0
gspread>=5.12.0
google-auth>=2.23.0
google-auth-oauthlib>=1.1.0
google-auth-httplib2>=0.1.1
"""

import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots
from dash import Dash, dcc, html, Input, Output
import gspread
from google.oauth2.service_account import Credentials
from datetime import datetime

# ==== CONFIGURATION ====
# Google Sheets Configuration
GOOGLE_SHEET_URL = "https://docs.google.com/spreadsheets/d/1Qrxl4_TUl-HDLBYXdbTznASOz_5FRtc-cUEeFFwspNo/edit"
# Or use sheet ID directly:
SHEET_ID = "YOUR_SHEET_ID_HERE"  
SHEET_NAME = "LN2 logs"  # Name of the sheet/tab to read from

# Service account credentials JSON file path
CREDENTIALS_FILE = "ln2-datalog-eb12001e2332.json"  # Download from Google Cloud Console

# Update interval in milliseconds (300000 ms = 5 minutes)
UPDATE_INTERVAL = 5 * 60 * 1000  # 5 minutes

# Liters per percent for tank volume calculation
LITERS_PER_PERCENT = 10

# User names for highlighting
USERS = ["Admin", "SB", "PQ", "CPB", "CSE", "LAM", "UAR1", "UAR2", "A&B", "LKB", "Guest"]

# Color palette for users (cycling colors)
USER_COLORS = {
    "Admin": "rgba(255, 0, 0, 0.15)",      # Red
    "SB": "rgba(0, 0, 255, 0.15)",         # Blue
    "PQ": "rgba(0, 128, 0, 0.15)",         # Green
    "CPB": "rgba(255, 165, 0, 0.15)",      # Orange
    "CSE": "rgba(128, 0, 128, 0.15)",      # Purple
    "LAM": "rgba(255, 192, 203, 0.15)",    # Pink
    "UAR1": "rgba(0, 255, 255, 0.15)",     # Cyan
    "UAR2": "rgba(255, 255, 0, 0.15)",     # Yellow
    "A&B": "rgba(128, 128, 128, 0.15)",    # Gray
    "LKB": "rgba(165, 42, 42, 0.15)",      # Brown
    "Guest": "rgba(0, 0, 0, 0.1)",         # Light black
}
# =======================


def setup_google_sheets_access():
    """
    Setup Google Sheets API access using service account credentials.
    
    To get credentials:
    1. Go to Google Cloud Console (console.cloud.google.com)
    2. Create a new project or select existing
    3. Enable Google Sheets API
    4. Create Service Account credentials
    5. Download JSON key file as 'credentials.json'
    6. Share the Google Sheet with the service account email
    """
    scopes = [
        'https://www.googleapis.com/auth/spreadsheets.readonly',
        'https://www.googleapis.com/auth/drive.readonly'
    ]
    
    creds = Credentials.from_service_account_file(CREDENTIALS_FILE, scopes=scopes)
    client = gspread.authorize(creds)
    
    return client


def load_data_from_sheets():
    """Load data from Google Sheets and return cleaned DataFrame."""
    try:
        # Connect to Google Sheets
        client = setup_google_sheets_access()
        
        # Open spreadsheet (use either URL or ID)
        try:
            sheet = client.open_by_url(GOOGLE_SHEET_URL).worksheet(SHEET_NAME)
        except:
            sheet = client.open_by_key(SHEET_ID).worksheet(SHEET_NAME)
        
        # Get all values
        data = sheet.get_all_values()
        
        # Convert to DataFrame
        df = pd.DataFrame(data[1:], columns=data[0])  # First row is header
        
        # Clean the data
        df = clean_data(df)
        
        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Loaded {len(df)} rows from Google Sheets")
        return df
        
    except Exception as e:
        print(f"Error loading data: {e}")
        return None


def clean_data(df):
    """Clean and format the DataFrame."""
    # Convert Date/Time to datetime
    df['Date/Time'] = pd.to_datetime(df['Date/Time'], errors='coerce')
    
    # Keep User column as-is (string)
    # Replace empty strings or 'None' string with None
    if 'User' in df.columns:
        df['User'] = df['User'].replace(['', 'None', 'none'], None)
    
    # Clean percentage columns (remove % sign and convert to float)
    percentage_cols = ['Tank 1', 'Tank 2', 'Humidity']
    for col in percentage_cols:
        if col in df.columns:
            df[col] = df[col].astype(str).str.replace('*UNDER*', '0').str.strip()
            df[col] = df[col].astype(str).str.replace('*OVER*', '100').str.strip()
            df[col] = df[col].astype(str).str.replace('%', '').str.strip()
            df[col] = pd.to_numeric(df[col], errors='coerce')
    
    # Temperature is already numeric
    if 'Temperature' in df.columns:
        df['Temperature'] = pd.to_numeric(df['Temperature'], errors='coerce')
    
    # Remove rows with invalid dates
    df = df.dropna(subset=['Date/Time'])
    
    return df


def extract_user_regions(df):
    """
    Extract regions where User is not None.
    Returns list of dicts with {user, start_time, end_time}
    """
    if 'User' not in df.columns:
        return []
    
    regions = []
    current_user = None
    start_time = None
    
    for idx, row in df.iterrows():
        user = row['User']
        time = row['Date/Time']
        
        if pd.notna(user) and user in USERS:
            # User activity detected
            if current_user != user:
                # New user or different user - save previous region if exists
                if current_user is not None and start_time is not None:
                    regions.append({
                        'user': current_user,
                        'start': start_time,
                        'end': prev_time
                    })
                # Start new region
                current_user = user
                start_time = time
        else:
            # No user or None - close current region if exists
            if current_user is not None and start_time is not None:
                regions.append({
                    'user': current_user,
                    'start': start_time,
                    'end': prev_time
                })
                current_user = None
                start_time = None
        
        prev_time = time
    
    # Close final region if still open
    if current_user is not None and start_time is not None:
        regions.append({
            'user': current_user,
            'start': start_time,
            'end': prev_time
        })
    
    return regions


def calculate_user_consumption(df, liters_per_percent=None):
    """
    Calculate liters consumed by each user.
    Returns dict with {user: liters}
    """
    if liters_per_percent is None:
        liters_per_percent = LITERS_PER_PERCENT
    
    if df is None or len(df) == 0 or 'User' not in df.columns:
        return {}
    
    consumption = {user: 0.0 for user in USERS}
    
    # Sort by time to ensure proper ordering
    df = df.sort_values('Date/Time').reset_index(drop=True)
    
    for i in range(1, len(df)):
        user = df.loc[i, 'User']
        
        if pd.notna(user) and user in USERS:
            # Calculate tank level differences from previous reading
            tank1_prev = df.loc[i-1, 'Tank 1']
            tank1_curr = df.loc[i, 'Tank 1']
            tank2_prev = df.loc[i-1, 'Tank 2']
            tank2_curr = df.loc[i, 'Tank 2']
            
            if pd.notna(tank1_prev) and pd.notna(tank1_curr):
                tank1_diff = tank1_prev - tank1_curr  # Positive = consumption
            else:
                tank1_diff = 0
            
            if pd.notna(tank2_prev) and pd.notna(tank2_curr):
                tank2_diff = tank2_prev - tank2_curr  # Positive = consumption
            else:
                tank2_diff = 0
            
            # Sum differences (only count positive = consumption)
            total_percent_diff = max(0, tank1_diff) + max(0, tank2_diff)
            
            # Convert to liters
            consumption[user] += total_percent_diff * liters_per_percent
    
    return consumption



def create_figure(df):
    """Create Plotly figure with 4 subplots."""
    if df is None or len(df) == 0:
        # Return empty figure if no data
        fig = go.Figure()
        fig.add_annotation(
            text="No data available or error loading from Google Sheets",
            xref="paper", yref="paper",
            x=0.5, y=0.5, showarrow=False,
            font=dict(size=20)
        )
        return fig
    
    # Create subplots
    fig = make_subplots(
        rows=4, cols=1,
        shared_xaxes=True,
        vertical_spacing=0.05,
        # subplot_titles=('Tank 1 Level', 'Tank 2 Level', 'Temperature (°C)', 'Humidity'),
    )
    
    # Tank 1
    fig.add_trace(
        go.Scattergl(
            x=df['Date/Time'],
            y=df['Tank 1'],
            mode='markers',
            name='Tank 1',
            line=dict(color='#1f77b4', width=1.5),
            hovertemplate='%{x}<br>Tank 1: %{y:.1f}%<extra></extra>'
        ),
        row=1, col=1
    )
    
    # Tank 2
    fig.add_trace(
        go.Scattergl(
            x=df['Date/Time'],
            y=df['Tank 2'],
            mode='markers',
            name='Tank 2',
            line=dict(color='#ff7f0e', width=1.5),
            hovertemplate='%{x}<br>Tank 2: %{y:.1f}%<extra></extra>'
        ),
        row=2, col=1
    )
    
    # Temperature
    fig.add_trace(
        go.Scattergl(
            x=df['Date/Time'],
            y=df['Temperature'],
            mode='markers',
            name='Temperature',
            line=dict(color='#d62728', width=1.5),
            hovertemplate='%{x}<br>Temperature: %{y:.1f}°C<extra></extra>'
        ),
        row=3, col=1
    )
    
    # Humidity
    fig.add_trace(
        go.Scattergl(
            x=df['Date/Time'],
            y=df['Humidity'],
            mode='markers',
            name='Humidity',
            line=dict(color='#2ca02c', width=1.5),
            hovertemplate='%{x}<br>Humidity: %{y:.1f}%<extra></extra>'
        ),
        row=4, col=1
    )
    
    fig.update_traces(marker_size=3, selector=dict(type='scattergl'))

    # Add user region rectangles and annotations
    user_regions = extract_user_regions(df)
    
    for region in user_regions:
        user = region['user']
        start = region['start']
        end = region['end']
        color = USER_COLORS.get(user, "rgba(200, 200, 200, 0.15)")
        
        # Add rectangle to each subplot (rows 1-4)
        for row_num in range(1, 5):
            fig.add_vrect(
                x0=start, x1=end,
                fillcolor=color,
                layer="below",
                line_width=0,
                row=row_num, col=1
            )
        
        # Add annotation only to the top subplot
        # Position annotation at the middle of the time range
        mid_time = start + (end - start) / 2
        
        fig.add_annotation(
            x=mid_time,
            y=1.08,  # Position above the plot
            xref='x',  # Reference to x-axis of first subplot
            yref='paper',  # Reference to paper coordinates
            text=user,
            showarrow=False,
            font=dict(size=10, color='black'),
            bgcolor='white',
            bordercolor='gray',
            borderwidth=1,
            borderpad=2,
            opacity=0.8
        )
    
    # Update y-axes labels
    fig.update_yaxes(title_text="Level (%)", row=1, col=1)
    fig.update_yaxes(title_text="Level (%)", row=2, col=1)
    fig.update_yaxes(title_text="Temp (°C)", row=3, col=1)
    fig.update_yaxes(title_text="Humidity (%)", row=4, col=1)
    
    # Update x-axis label
    fig.update_xaxes(title_text="Date/Time", row=4, col=1)
    
    # Update layout
    fig.update_layout(
        height=700,
        title_text=f"Tank Monitoring Data - Live from Google Sheets (Last updated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')})",
        hovermode='x unified',
        showlegend=True,
        template='plotly_white',
        legend=dict(
            itemsizing='constant',
            orientation="h",
            yanchor="bottom",
            y=1.07,
            xanchor="right",
            x=1
        )
    )
    
    return fig


# Initialize Dash app
app = Dash(__name__)

# App layout
app.layout = html.Div([
    # Header with just title and status
    html.Div([
        html.H1("Tank Monitoring Dashboard - Live from Google Sheets", 
                style={'margin': 0, 'color': '#2c3e50'}),
        html.Div(id='status-text', 
                style={'color': '#7f8c8d', 'marginTop': 5}),
        html.Div(f"Auto-refresh interval: {UPDATE_INTERVAL / 60000:.0f} minutes", 
                style={'color': '#95a5a6', 'fontSize': 12}),
    ], style={'padding': '20px', 'paddingBottom': '10px'}),
    
    # Main content: graph on left, controls on right
    html.Div([
        # Left - graph
        dcc.Graph(id='live-graph', style={'flex': 1}),
        
        # Right - date range and stats
        html.Div([
            # Liters per percent input
            html.Div([
                html.Label("Liters per %:", style={'fontWeight': 'bold', 'marginBottom': 5, 'display': 'block'}),
                dcc.Input(
                    id='liters-per-percent-input',
                    type='number',
                    value=LITERS_PER_PERCENT,
                    step=0.1,
                    style={'width': '100%', 'fontSize': 12}
                )
            ], style={'padding': '10px', 'backgroundColor': '#f8f9fa', 'borderRadius': '5px', 'marginBottom': '15px'}),
            # Date range filter
            html.Div([
                html.Div("Date Range:", style={'fontWeight': 'bold', 'marginBottom': 5}),
                dcc.RadioItems(
                    id='range-selector',
                    options=[
                        {'label': 'Last 24 hours', 'value': '24h'},
                        {'label': 'Last 7 days', 'value': '7d'},
                        {'label': 'Last 14 days', 'value': '14d'},
                        {'label': 'Last 30 days', 'value': '30d'},
                        {'label': 'This year', 'value': 'year'},
                        {'label': 'Month:', 'value': 'month'}
                    ],
                    value='7d',
                    style={'fontSize': 12}
                ),
                dcc.Dropdown(
                    id='month-selector',
                    options=[
                        {'label': 'January', 'value': 1},
                        {'label': 'February', 'value': 2},
                        {'label': 'March', 'value': 3},
                        {'label': 'April', 'value': 4},
                        {'label': 'May', 'value': 5},
                        {'label': 'June', 'value': 6},
                        {'label': 'July', 'value': 7},
                        {'label': 'August', 'value': 8},
                        {'label': 'September', 'value': 9},
                        {'label': 'October', 'value': 10},
                        {'label': 'November', 'value': 11},
                        {'label': 'December', 'value': 12}
                    ],
                    value=datetime.now().month,
                    style={'marginTop': 5, 'fontSize': 11}
                )
            ], style={'padding': '10px', 'backgroundColor': '#f8f9fa', 'borderRadius': '5px', 'marginBottom': '15px'}),
            
            # User statistics
            html.Div(id='user-stats', 
                    style={
                        'padding': '10px',
                        'backgroundColor': '#f8f9fa',
                        'borderRadius': '5px',
                        'fontSize': '12px'
                    }),
        ], style={'width': '200px', 'marginLeft': '15px'}),
        
    ], style={'display': 'flex', 'paddingLeft': '20px', 'paddingRight': '20px', 'height': '80vh'}),
    
    # Hidden store and interval (unchanged)
    dcc.Store(id='full-data-store'),
    dcc.Interval(
        id='interval-component',
        interval=UPDATE_INTERVAL,
        n_intervals=0
    )
])

@app.callback(
    [Output('full-data-store', 'data'),
     Output('status-text', 'children')],
    [Input('interval-component', 'n_intervals')]
)
def load_data(n):
    """Load data from Google Sheets and store."""
    df = load_data_from_sheets()
    
    if df is not None and len(df) > 0:
        status = f"✓ Data loaded: {len(df)} rows | Date range: {df['Date/Time'].min()} to {df['Date/Time'].max()}"
        # Convert to JSON for storage
        return df.to_json(date_format='iso', orient='split'), status
    else:
        return None, "⚠ Error loading data from Google Sheets"


@app.callback(
    [Output('live-graph', 'figure'),
     Output('user-stats', 'children')],
    [Input('full-data-store', 'data'),
     Input('range-selector', 'value'),
     Input('month-selector', 'value'),
     Input('liters-per-percent-input', 'value')]
)
def update_graph(data_json, range_value, month_value, liters_per_percent):
    """Filter data and update graph based on selected date range."""
    if data_json is None:
        fig = go.Figure()
        fig.add_annotation(text="Loading...", xref="paper", yref="paper",
                          x=0.5, y=0.5, showarrow=False, font=dict(size=20))
        return fig, []

    if liters_per_percent is None or liters_per_percent <= 0:
        liters_per_percent = LITERS_PER_PERCENT

    # Load full dataset
    df = pd.read_json(data_json, orient='split')
    df['Date/Time'] = pd.to_datetime(df['Date/Time'])
    
    # Filter based on selection
    now = datetime.now()
    if range_value == '24h':
        cutoff = now - pd.Timedelta(hours=24)
        df_filtered = df[df['Date/Time'] >= cutoff]
    elif range_value == '7d':
        cutoff = now - pd.Timedelta(days=7)
        df_filtered = df[df['Date/Time'] >= cutoff]
    elif range_value == '14d':
        cutoff = now - pd.Timedelta(days=14)
        df_filtered = df[df['Date/Time'] >= cutoff]
    elif range_value == '30d':
        cutoff = now - pd.Timedelta(days=30)
        df_filtered = df[df['Date/Time'] >= cutoff]
    elif range_value == 'year':
        year = now.year
        df_filtered = df[(df['Date/Time'].dt.year == year)]
    elif range_value == 'month':
        year = now.year
        df_filtered = df[(df['Date/Time'].dt.year == year) & 
                         (df['Date/Time'].dt.month == month_value)]
    else:
        df_filtered = df
    
    # Create figure
    fig = create_figure(df_filtered)
    
    # Calculate consumption on filtered data with custom liters_per_percent
    consumption = calculate_user_consumption(df_filtered, liters_per_percent)
       
    # Create user stats display
    stats_children = [html.Div("User Consumption (L):", style={'fontWeight': 'bold', 'marginBottom': '8px'})]
    for user in USERS:
        liters = consumption.get(user, 0.0)
        if liters > 0:
            stats_children.append(
                html.Div(f"{user}: {liters:.1f} L", 
                        style={'marginBottom': '3px'})
            )
    
    return fig, stats_children


if __name__ == '__main__':
    print("=" * 60)
    print("Tank Monitoring Dashboard - Google Sheets Edition")
    print("=" * 60)
    print(f"\nUpdate interval: {UPDATE_INTERVAL / 60000:.0f} minutes")
    print("\nStarting dashboard...")
    print("Open your browser to: http://127.0.0.1:8050")
    print("\nPress Ctrl+C to stop the server")
    print("=" * 60)
    
    # Run the Dash app
    app.run(debug=True, host='0.0.0.0', port=8050)