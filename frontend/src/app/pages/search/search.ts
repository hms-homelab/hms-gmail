import { Component, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { Router } from '@angular/router';
import { CommonModule } from '@angular/common';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { MatButtonModule } from '@angular/material/button';
import { MatButtonToggleModule } from '@angular/material/button-toggle';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressSpinnerModule } from '@angular/material/progress-spinner';
import { GmailService } from '../../services/gmail.service';
import { SearchResult } from '../../models/email.model';

@Component({
  selector: 'app-search',
  standalone: true,
  imports: [
    CommonModule, FormsModule,
    MatFormFieldModule, MatInputModule, MatButtonModule,
    MatButtonToggleModule, MatIconModule, MatProgressSpinnerModule
  ],
  template: `
    <div class="page-container">
      <div class="search-box card">
        <div class="search-row">
          <mat-form-field appearance="outline" class="search-field">
            <mat-label>Search emails</mat-label>
            <input matInput [(ngModel)]="query" (keyup.enter)="doSearch()"
                   placeholder="e.g. invoice from Google" />
            <mat-icon matSuffix>search</mat-icon>
          </mat-form-field>
          <button mat-flat-button color="primary" (click)="doSearch()" [disabled]="loading()">
            Search
          </button>
        </div>
        <div class="mode-row">
          <mat-button-toggle-group [(ngModel)]="mode" aria-label="Search mode">
            <mat-button-toggle value="hybrid">Hybrid</mat-button-toggle>
            <mat-button-toggle value="fts">Full-text</mat-button-toggle>
            <mat-button-toggle value="vector">Semantic</mat-button-toggle>
          </mat-button-toggle-group>
          <span class="result-count" *ngIf="results().length > 0">
            {{ results().length }} results
          </span>
        </div>
      </div>

      <div *ngIf="loading()" class="loading-center">
        <mat-spinner diameter="40"></mat-spinner>
      </div>

      <div *ngIf="error()" class="error-msg">{{ error() }}</div>

      <div *ngFor="let r of results()" class="result-card card" (click)="open(r)">
        <div class="result-header">
          <span class="result-subject">{{ r.subject || '(no subject)' }}</span>
          <span class="result-date">{{ r.date | date:'MMM d, y' }}</span>
        </div>
        <div class="result-from">{{ r.from }}</div>
        <div class="result-snippet">{{ r.snippet }}</div>
        <div class="result-meta">
          <span *ngIf="r.has_attachment" class="attach-badge">
            <mat-icon inline>attach_file</mat-icon> attachment
          </span>
          <span class="score">score {{ r.score | number:'1.3-3' }}</span>
        </div>
      </div>

      <div *ngIf="!loading() && searched && results().length === 0" class="empty-msg">
        No results found.
      </div>
    </div>
  `,
  styles: [`
    .search-row { display: flex; gap: 12px; align-items: flex-start; }
    .search-field { flex: 1; }
    .mode-row { display: flex; align-items: center; gap: 16px; margin-top: -8px; }
    .result-count { font-size: 13px; color: #888; }
    .loading-center { display: flex; justify-content: center; padding: 40px; }
    .error-msg { color: #f44336; padding: 16px; }
    .result-card {
      cursor: pointer;
      transition: background 0.15s;
      &:hover { background: #2a2a2a; }
    }
    .result-header { display: flex; justify-content: space-between; align-items: baseline; }
    .result-subject { font-size: 15px; font-weight: 500; color: #e0e0e0; }
    .result-date { font-size: 12px; color: #888; }
    .result-from { font-size: 13px; color: #aaa; margin: 4px 0; }
    .result-snippet { font-size: 13px; color: #999; line-height: 1.5; }
    .result-meta { display: flex; align-items: center; gap: 12px; margin-top: 8px; font-size: 12px; color: #666; }
    .attach-badge { display: flex; align-items: center; gap: 2px; }
    .score { margin-left: auto; }
    .empty-msg { text-align: center; color: #666; padding: 40px; font-size: 14px; }
  `]
})
export class SearchPage {
  private router = inject(Router);
  private gmail = inject(GmailService);

  query = '';
  mode = 'hybrid';
  results = signal<SearchResult[]>([]);
  loading = signal(false);
  error = signal('');
  searched = false;

  doSearch() {
    if (!this.query.trim()) return;
    this.loading.set(true);
    this.error.set('');
    this.searched = true;
    this.gmail.search(this.query.trim(), 20, this.mode).subscribe({
      next: (res) => {
        this.results.set(res.results);
        this.loading.set(false);
      },
      error: (err) => {
        this.error.set(err.message || 'Search failed');
        this.loading.set(false);
      }
    });
  }

  open(r: SearchResult) {
    this.router.navigate(['/emails', r.id]);
  }
}
