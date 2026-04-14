#!/usr/bin/env python3
"""
Advanced CAN Latency Data Collector with Real-Time Analysis
"""

import serial
import datetime
import sys
import csv
import statistics

SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 115200

class LatencyAnalyzer:
    def __init__(self):
        self.latencies = []
        self.jitters = []
        self.output_file = f"latency_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        self.csv_writer = None
        self.file_handle = None
        
    def start(self):
        """Open serial port and CSV file"""
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
            self.file_handle = open(self.output_file, 'w', newline='')
            self.csv_writer = csv.writer(self.file_handle)
            
            # Write header
            self.csv_writer.writerow(['SEQ', 'TX_TIME_US', 'RX_TIME_US', 'LATENCY_US', 'JITTER_US'])
            self.file_handle.flush()
            
            print(f"✓ Serial port: {SERIAL_PORT}")
            print(f"✓ Logging to: {self.output_file}")
            print(f"✓ Press Ctrl+C to stop and view statistics\n")
            print("-" * 80)
            
        except Exception as e:
            print(f"Error: {e}")
            sys.exit(1)
    
    def process_csv_line(self, line):
        """Extract and save CSV data"""
        try:
            # Parse: CSV,SEQ,TX_TIME,RX_TIME,LATENCY,JITTER
            parts = line.split(',')
            if len(parts) >= 6:
                seq, tx_time, rx_time, latency, jitter = parts[1:6]
                
                # Save to file
                self.csv_writer.writerow([seq, tx_time, rx_time, latency, jitter])
                self.file_handle.flush()
                
                # Store for statistics
                self.latencies.append(int(latency))
                self.jitters.append(int(jitter))
                
                return True
        except Exception as e:
            print(f"Parse error: {e}")
        return False
    
    def print_statistics(self):
        """Print final statistics"""
        if not self.latencies:
            print("\nNo data collected")
            return
        
        print("\n" + "=" * 80)
        print("COLLECTED DATA STATISTICS")
        print("=" * 80)
        print(f"Total Messages:     {len(self.latencies)}")
        print(f"Data File:          {self.output_file}")
        print("-" * 80)
        print("Latency (µs):")
        print(f"  Min:              {min(self.latencies)}")
        print(f"  Max:              {max(self.latencies)}")
        print(f"  Mean:             {statistics.mean(self.latencies):.2f}")
        print(f"  Median:           {statistics.median(self.latencies):.2f}")
        print(f"  Std Dev:          {statistics.stdev(self.latencies):.2f}")
        print("-" * 80)
        print("Jitter (µs):")
        print(f"  Max:              {max(self.jitters)}")
        print(f"  Mean:             {statistics.mean(self.jitters):.2f}")
        print(f"  Median:           {statistics.median(self.jitters):.2f}")
        print("=" * 80)
    
    def run(self):
        """Main collection loop"""
        self.start()
        
        try:
            while True:
                if self.ser.in_waiting > 0:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    # Always print to console
                    print(line)
                    
                    # Process CSV lines
                    if line.startswith('CSV,'):
                        self.process_csv_line(line)
                        
        except KeyboardInterrupt:
            self.print_statistics()
        finally:
            self.cleanup()
    
    def cleanup(self):
        """Close resources"""
        if self.file_handle:
            self.file_handle.close()
        if hasattr(self, 'ser'):
            self.ser.close()
        print(f"\n✓ Data saved to {self.output_file}")

if __name__ == "__main__":
    analyzer = LatencyAnalyzer()
    analyzer.run()